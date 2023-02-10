// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <mali_kbase.h>
#include <linux/slab.h>
#include <csf/mali_kbase_csf_registers.h>
#include <csf/mali_kbase_csf_firmware.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <linux/mali_kbase_debug_coresight_csf.h>
#include <debug/backend/mali_kbase_debug_coresight_internal_csf.h>

static const char *coresight_state_to_string(enum kbase_debug_coresight_csf_state state)
{
	switch (state) {
	case KBASE_DEBUG_CORESIGHT_CSF_DISABLED:
		return "DISABLED";
	case KBASE_DEBUG_CORESIGHT_CSF_ENABLED:
		return "ENABLED";
	default:
		break;
	}

	return "UNKNOWN";
}

static bool validate_reg_addr(struct kbase_debug_coresight_csf_client *client,
			      struct kbase_device *kbdev, u32 reg_addr, u8 op_type)
{
	int i;

	if (reg_addr & 0x3) {
		dev_err(kbdev->dev, "Invalid operation %d: reg_addr (0x%x) not 32bit aligned",
			op_type, reg_addr);
		return false;
	}

	for (i = 0; i < client->nr_ranges; i++) {
		struct kbase_debug_coresight_csf_address_range *range = &client->addr_ranges[i];

		if ((range->start <= reg_addr) && (reg_addr <= range->end))
			return true;
	}

	dev_err(kbdev->dev, "Invalid operation %d: reg_addr (0x%x) not in client range", op_type,
		reg_addr);

	return false;
}

static bool validate_op(struct kbase_debug_coresight_csf_client *client,
			struct kbase_debug_coresight_csf_op *op)
{
	struct kbase_device *kbdev;
	u32 reg;

	if (!op)
		return false;

	if (!client)
		return false;

	kbdev = (struct kbase_device *)client->drv_data;

	switch (op->type) {
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_NOP:
		return true;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE_IMM:
		if (validate_reg_addr(client, kbdev, op->op.write_imm.reg_addr, op->type))
			return true;

		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE_IMM_RANGE:
		for (reg = op->op.write_imm_range.reg_start; reg <= op->op.write_imm_range.reg_end;
		     reg += sizeof(u32)) {
			if (!validate_reg_addr(client, kbdev, reg, op->type))
				return false;
		}

		return true;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE:
		if (!op->op.write.ptr) {
			dev_err(kbdev->dev, "Invalid operation %d: ptr not set", op->type);
			break;
		}

		if (validate_reg_addr(client, kbdev, op->op.write.reg_addr, op->type))
			return true;

		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_READ:
		if (!op->op.read.ptr) {
			dev_err(kbdev->dev, "Invalid operation %d: ptr not set", op->type);
			break;
		}

		if (validate_reg_addr(client, kbdev, op->op.read.reg_addr, op->type))
			return true;

		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_POLL:
		if (validate_reg_addr(client, kbdev, op->op.poll.reg_addr, op->type))
			return true;

		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_AND:
		fallthrough;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_OR:
		fallthrough;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_XOR:
		fallthrough;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_NOT:
		if (op->op.bitw.ptr != NULL)
			return true;

		dev_err(kbdev->dev, "Invalid bitwise operation pointer");

		break;
	default:
		dev_err(kbdev->dev, "Invalid operation %d", op->type);
		break;
	}

	return false;
}

static bool validate_seq(struct kbase_debug_coresight_csf_client *client,
			 struct kbase_debug_coresight_csf_sequence *seq)
{
	struct kbase_debug_coresight_csf_op *ops = seq->ops;
	int nr_ops = seq->nr_ops;
	int i;

	for (i = 0; i < nr_ops; i++) {
		if (!validate_op(client, &ops[i]))
			return false;
	}

	return true;
}

static int execute_op(struct kbase_device *kbdev, struct kbase_debug_coresight_csf_op *op)
{
	int result = -EINVAL;
	u32 reg;

	dev_dbg(kbdev->dev, "Execute operation %d", op->type);

	switch (op->type) {
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_NOP:
		result = 0;
		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE_IMM:
		result = kbase_csf_firmware_mcu_register_write(kbdev, op->op.write.reg_addr,
							       op->op.write_imm.val);
		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE_IMM_RANGE:
		for (reg = op->op.write_imm_range.reg_start; reg <= op->op.write_imm_range.reg_end;
		     reg += sizeof(u32)) {
			result = kbase_csf_firmware_mcu_register_write(kbdev, reg,
								       op->op.write_imm_range.val);
			if (!result)
				break;
		}
		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_WRITE:
		result = kbase_csf_firmware_mcu_register_write(kbdev, op->op.write.reg_addr,
							       *op->op.write.ptr);
		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_READ:
		result = kbase_csf_firmware_mcu_register_read(kbdev, op->op.read.reg_addr,
							      op->op.read.ptr);
		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_POLL:
		result = kbase_csf_firmware_mcu_register_poll(kbdev, op->op.poll.reg_addr,
							      op->op.poll.mask, op->op.poll.val);
		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_AND:
		*op->op.bitw.ptr &= op->op.bitw.val;
		result = 0;
		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_OR:
		*op->op.bitw.ptr |= op->op.bitw.val;
		result = 0;
		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_XOR:
		*op->op.bitw.ptr ^= op->op.bitw.val;
		result = 0;
		break;
	case KBASE_DEBUG_CORESIGHT_CSF_OP_TYPE_BIT_NOT:
		*op->op.bitw.ptr = ~(*op->op.bitw.ptr);
		result = 0;
		break;
	default:
		dev_err(kbdev->dev, "Invalid operation %d", op->type);
		break;
	}

	return result;
}

static int coresight_config_enable(struct kbase_device *kbdev,
				   struct kbase_debug_coresight_csf_config *config)
{
	int ret = 0;
	int i;

	if (!config)
		return -EINVAL;

	if (config->state == KBASE_DEBUG_CORESIGHT_CSF_ENABLED)
		return ret;

	for (i = 0; config->enable_seq && !ret && i < config->enable_seq->nr_ops; i++)
		ret = execute_op(kbdev, &config->enable_seq->ops[i]);

	if (!ret) {
		dev_dbg(kbdev->dev, "Coresight config (0x%pK) state transition: %s to %s", config,
			coresight_state_to_string(config->state),
			coresight_state_to_string(KBASE_DEBUG_CORESIGHT_CSF_ENABLED));
		config->state = KBASE_DEBUG_CORESIGHT_CSF_ENABLED;
	}

	/* Always assign the return code during config enable.
	 * It gets propagated when calling config disable.
	 */
	config->error = ret;

	return ret;
}

static int coresight_config_disable(struct kbase_device *kbdev,
				    struct kbase_debug_coresight_csf_config *config)
{
	int ret = 0;
	int i;

	if (!config)
		return -EINVAL;

	if (config->state == KBASE_DEBUG_CORESIGHT_CSF_DISABLED)
		return ret;

	for (i = 0; config->disable_seq && !ret && i < config->disable_seq->nr_ops; i++)
		ret = execute_op(kbdev, &config->disable_seq->ops[i]);

	if (!ret) {
		dev_dbg(kbdev->dev, "Coresight config (0x%pK) state transition: %s to %s", config,
			coresight_state_to_string(config->state),
			coresight_state_to_string(KBASE_DEBUG_CORESIGHT_CSF_DISABLED));
		config->state = KBASE_DEBUG_CORESIGHT_CSF_DISABLED;
	} else {
		/* Only assign the error if ret is not 0.
		 * As we don't want to overwrite an error from config enable
		 */
		if (!config->error)
			config->error = ret;
	}

	return ret;
}

void *kbase_debug_coresight_csf_register(void *drv_data,
					 struct kbase_debug_coresight_csf_address_range *ranges,
					 int nr_ranges)
{
	struct kbase_debug_coresight_csf_client *client, *client_entry;
	struct kbase_device *kbdev;
	unsigned long flags;
	int k;

	if (unlikely(!drv_data)) {
		pr_err("NULL drv_data");
		return NULL;
	}

	kbdev = (struct kbase_device *)drv_data;

	if (unlikely(!ranges)) {
		dev_err(kbdev->dev, "NULL ranges");
		return NULL;
	}

	if (unlikely(!nr_ranges)) {
		dev_err(kbdev->dev, "nr_ranges is 0");
		return NULL;
	}

	for (k = 0; k < nr_ranges; k++) {
		if (ranges[k].end < ranges[k].start) {
			dev_err(kbdev->dev, "Invalid address ranges 0x%08x - 0x%08x",
				ranges[k].start, ranges[k].end);
			return NULL;
		}
	}

	client = kzalloc(sizeof(struct kbase_debug_coresight_csf_client), GFP_KERNEL);

	if (!client)
		return NULL;

	spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);
	list_for_each_entry(client_entry, &kbdev->csf.coresight.clients, link) {
		struct kbase_debug_coresight_csf_address_range *client_ranges =
			client_entry->addr_ranges;
		int i;

		for (i = 0; i < client_entry->nr_ranges; i++) {
			int j;

			for (j = 0; j < nr_ranges; j++) {
				if ((ranges[j].start < client_ranges[i].end) &&
				    (client_ranges[i].start < ranges[j].end)) {
					spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);
					kfree(client);
					dev_err(kbdev->dev,
						"Client with range 0x%08x - 0x%08x already present at address range 0x%08x - 0x%08x",
						client_ranges[i].start, client_ranges[i].end,
						ranges[j].start, ranges[j].end);

					return NULL;
				}
			}
		}
	}

	client->drv_data = drv_data;
	client->addr_ranges = ranges;
	client->nr_ranges = nr_ranges;
	list_add(&client->link, &kbdev->csf.coresight.clients);
	spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);

	return client;
}
EXPORT_SYMBOL(kbase_debug_coresight_csf_register);

void kbase_debug_coresight_csf_unregister(void *client_data)
{
	struct kbase_debug_coresight_csf_client *client;
	struct kbase_debug_coresight_csf_config *config_entry;
	struct kbase_device *kbdev;
	unsigned long flags;
	bool retry = true;

	if (unlikely(!client_data)) {
		pr_err("NULL client");
		return;
	}

	client = (struct kbase_debug_coresight_csf_client *)client_data;

	kbdev = (struct kbase_device *)client->drv_data;
	if (unlikely(!kbdev)) {
		pr_err("NULL drv_data in client");
		return;
	}

	/* check for active config from client */
	spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);
	list_del_init(&client->link);

	while (retry && !list_empty(&kbdev->csf.coresight.configs)) {
		retry = false;
		list_for_each_entry(config_entry, &kbdev->csf.coresight.configs, link) {
			if (config_entry->client == client) {
				spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);
				kbase_debug_coresight_csf_config_free(config_entry);
				spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);
				retry = true;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);

	kfree(client);
}
EXPORT_SYMBOL(kbase_debug_coresight_csf_unregister);

void *
kbase_debug_coresight_csf_config_create(void *client_data,
					struct kbase_debug_coresight_csf_sequence *enable_seq,
					struct kbase_debug_coresight_csf_sequence *disable_seq)
{
	struct kbase_debug_coresight_csf_client *client;
	struct kbase_debug_coresight_csf_config *config;
	struct kbase_device *kbdev;

	if (unlikely(!client_data)) {
		pr_err("NULL client");
		return NULL;
	}

	client = (struct kbase_debug_coresight_csf_client *)client_data;

	kbdev = (struct kbase_device *)client->drv_data;
	if (unlikely(!kbdev)) {
		pr_err("NULL drv_data in client");
		return NULL;
	}

	if (enable_seq) {
		if (!validate_seq(client, enable_seq)) {
			dev_err(kbdev->dev, "Invalid enable_seq");
			return NULL;
		}
	}

	if (disable_seq) {
		if (!validate_seq(client, disable_seq)) {
			dev_err(kbdev->dev, "Invalid disable_seq");
			return NULL;
		}
	}

	config = kzalloc(sizeof(struct kbase_debug_coresight_csf_config), GFP_KERNEL);
	if (WARN_ON(!client))
		return NULL;

	config->client = client;
	config->enable_seq = enable_seq;
	config->disable_seq = disable_seq;
	config->error = 0;
	config->state = KBASE_DEBUG_CORESIGHT_CSF_DISABLED;

	INIT_LIST_HEAD(&config->link);

	return config;
}
EXPORT_SYMBOL(kbase_debug_coresight_csf_config_create);

void kbase_debug_coresight_csf_config_free(void *config_data)
{
	struct kbase_debug_coresight_csf_config *config;

	if (unlikely(!config_data)) {
		pr_err("NULL config");
		return;
	}

	config = (struct kbase_debug_coresight_csf_config *)config_data;

	kbase_debug_coresight_csf_config_disable(config);

	kfree(config);
}
EXPORT_SYMBOL(kbase_debug_coresight_csf_config_free);

int kbase_debug_coresight_csf_config_enable(void *config_data)
{
	struct kbase_debug_coresight_csf_config *config;
	struct kbase_debug_coresight_csf_client *client;
	struct kbase_device *kbdev;
	struct kbase_debug_coresight_csf_config *config_entry;
	unsigned long flags;
	int ret = 0;

	if (unlikely(!config_data)) {
		pr_err("NULL config");
		return -EINVAL;
	}

	config = (struct kbase_debug_coresight_csf_config *)config_data;
	client = (struct kbase_debug_coresight_csf_client *)config->client;

	if (unlikely(!client)) {
		pr_err("NULL client in config");
		return -EINVAL;
	}

	kbdev = (struct kbase_device *)client->drv_data;
	if (unlikely(!kbdev)) {
		pr_err("NULL drv_data in client");
		return -EINVAL;
	}

	/* Check to prevent double entry of config */
	spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);
	list_for_each_entry(config_entry, &kbdev->csf.coresight.configs, link) {
		if (config_entry == config) {
			spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);
			dev_err(kbdev->dev, "Config already enabled");
			return -EINVAL;
		}
	}
	spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);

	kbase_csf_scheduler_lock(kbdev);
	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	/* Check the state of Scheduler to confirm the desired state of MCU */
	if (((kbdev->csf.scheduler.state != SCHED_SUSPENDED) &&
	     (kbdev->csf.scheduler.state != SCHED_SLEEPING) &&
	     !kbase_csf_scheduler_protected_mode_in_use(kbdev)) ||
	    kbase_pm_get_policy(kbdev) == &kbase_pm_always_on_policy_ops) {
		kbase_csf_scheduler_spin_unlock(kbdev, flags);
		/* Wait for MCU to reach the stable ON state */
		ret = kbase_pm_wait_for_desired_state(kbdev);

		if (ret)
			dev_err(kbdev->dev,
				"Wait for PM state failed when enabling coresight config");
		else
			ret = coresight_config_enable(kbdev, config);

		kbase_csf_scheduler_spin_lock(kbdev, &flags);
	}

	/* Add config to next enable sequence */
	if (!ret) {
		spin_lock(&kbdev->csf.coresight.lock);
		list_add(&config->link, &kbdev->csf.coresight.configs);
		spin_unlock(&kbdev->csf.coresight.lock);
	}

	kbase_csf_scheduler_spin_unlock(kbdev, flags);
	kbase_csf_scheduler_unlock(kbdev);

	return ret;
}
EXPORT_SYMBOL(kbase_debug_coresight_csf_config_enable);

int kbase_debug_coresight_csf_config_disable(void *config_data)
{
	struct kbase_debug_coresight_csf_config *config;
	struct kbase_debug_coresight_csf_client *client;
	struct kbase_device *kbdev;
	struct kbase_debug_coresight_csf_config *config_entry;
	bool found_in_list = false;
	unsigned long flags;
	int ret = 0;

	if (unlikely(!config_data)) {
		pr_err("NULL config");
		return -EINVAL;
	}

	config = (struct kbase_debug_coresight_csf_config *)config_data;

	/* Exit early if not enabled prior */
	if (list_empty(&config->link))
		return ret;

	client = (struct kbase_debug_coresight_csf_client *)config->client;

	if (unlikely(!client)) {
		pr_err("NULL client in config");
		return -EINVAL;
	}

	kbdev = (struct kbase_device *)client->drv_data;
	if (unlikely(!kbdev)) {
		pr_err("NULL drv_data in client");
		return -EINVAL;
	}

	/* Check if the config is in the correct list */
	spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);
	list_for_each_entry(config_entry, &kbdev->csf.coresight.configs, link) {
		if (config_entry == config) {
			found_in_list = true;
			break;
		}
	}
	spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);

	if (!found_in_list) {
		dev_err(kbdev->dev, "Config looks corrupted");
		return -EINVAL;
	}

	kbase_csf_scheduler_lock(kbdev);
	kbase_csf_scheduler_spin_lock(kbdev, &flags);

	/* Check the state of Scheduler to confirm the desired state of MCU */
	if (((kbdev->csf.scheduler.state != SCHED_SUSPENDED) &&
	     (kbdev->csf.scheduler.state != SCHED_SLEEPING) &&
	     !kbase_csf_scheduler_protected_mode_in_use(kbdev)) ||
	    kbase_pm_get_policy(kbdev) == &kbase_pm_always_on_policy_ops) {
		kbase_csf_scheduler_spin_unlock(kbdev, flags);
		/* Wait for MCU to reach the stable ON state */
		ret = kbase_pm_wait_for_desired_state(kbdev);

		if (ret)
			dev_err(kbdev->dev,
				"Wait for PM state failed when disabling coresight config");
		else
			ret = coresight_config_disable(kbdev, config);

		kbase_csf_scheduler_spin_lock(kbdev, &flags);
	} else if (kbdev->pm.backend.mcu_state == KBASE_MCU_OFF) {
		/* MCU is OFF, so the disable sequence was already executed.
		 *
		 * Propagate any error that would have occurred during the enable
		 * or disable sequence.
		 *
		 * This is done as part of the disable sequence, since the call from
		 * client is synchronous.
		 */
		ret = config->error;
	}

	/* Remove config from next disable sequence */
	spin_lock(&kbdev->csf.coresight.lock);
	list_del_init(&config->link);
	spin_unlock(&kbdev->csf.coresight.lock);

	kbase_csf_scheduler_spin_unlock(kbdev, flags);
	kbase_csf_scheduler_unlock(kbdev);

	return ret;
}
EXPORT_SYMBOL(kbase_debug_coresight_csf_config_disable);

static void coresight_config_enable_all(struct work_struct *data)
{
	struct kbase_device *kbdev =
		container_of(data, struct kbase_device, csf.coresight.enable_work);
	struct kbase_debug_coresight_csf_config *config_entry;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);

	list_for_each_entry(config_entry, &kbdev->csf.coresight.configs, link) {
		spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);
		if (coresight_config_enable(kbdev, config_entry))
			dev_err(kbdev->dev, "enable config (0x%pK) failed", config_entry);
		spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);
	}

	spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_pm_update_state(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	wake_up_all(&kbdev->csf.coresight.event_wait);
}

static void coresight_config_disable_all(struct work_struct *data)
{
	struct kbase_device *kbdev =
		container_of(data, struct kbase_device, csf.coresight.disable_work);
	struct kbase_debug_coresight_csf_config *config_entry;
	unsigned long flags;

	spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);

	list_for_each_entry(config_entry, &kbdev->csf.coresight.configs, link) {
		spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);
		if (coresight_config_disable(kbdev, config_entry))
			dev_err(kbdev->dev, "disable config (0x%pK) failed", config_entry);
		spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);
	}

	spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	kbase_pm_update_state(kbdev);
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	wake_up_all(&kbdev->csf.coresight.event_wait);
}

void kbase_debug_coresight_csf_disable_pmode_enter(struct kbase_device *kbdev)
{
	unsigned long flags;

	dev_dbg(kbdev->dev, "Coresight state %s before protected mode enter",
		coresight_state_to_string(KBASE_DEBUG_CORESIGHT_CSF_ENABLED));

	lockdep_assert_held(&kbdev->csf.scheduler.lock);

	kbase_pm_lock(kbdev);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	kbdev->csf.coresight.disable_on_pmode_enter = true;
	kbdev->csf.coresight.enable_on_pmode_exit = false;
	kbase_pm_update_state(kbdev);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	kbase_pm_wait_for_desired_state(kbdev);

	kbase_pm_unlock(kbdev);
}

void kbase_debug_coresight_csf_enable_pmode_exit(struct kbase_device *kbdev)
{
	dev_dbg(kbdev->dev, "Coresight state %s after protected mode exit",
		coresight_state_to_string(KBASE_DEBUG_CORESIGHT_CSF_DISABLED));

	lockdep_assert_held(&kbdev->hwaccess_lock);

	WARN_ON(kbdev->csf.coresight.disable_on_pmode_enter);

	kbdev->csf.coresight.enable_on_pmode_exit = true;
	kbase_pm_update_state(kbdev);
}

void kbase_debug_coresight_csf_state_request(struct kbase_device *kbdev,
					     enum kbase_debug_coresight_csf_state state)
{
	if (unlikely(!kbdev))
		return;

	if (unlikely(!kbdev->csf.coresight.workq))
		return;

	dev_dbg(kbdev->dev, "Coresight state %s requested", coresight_state_to_string(state));

	switch (state) {
	case KBASE_DEBUG_CORESIGHT_CSF_DISABLED:
		queue_work(kbdev->csf.coresight.workq, &kbdev->csf.coresight.disable_work);
		break;
	case KBASE_DEBUG_CORESIGHT_CSF_ENABLED:
		queue_work(kbdev->csf.coresight.workq, &kbdev->csf.coresight.enable_work);
		break;
	default:
		dev_err(kbdev->dev, "Invalid Coresight state %d", state);
		break;
	}
}

bool kbase_debug_coresight_csf_state_check(struct kbase_device *kbdev,
					   enum kbase_debug_coresight_csf_state state)
{
	struct kbase_debug_coresight_csf_config *config_entry;
	unsigned long flags;
	bool success = true;

	dev_dbg(kbdev->dev, "Coresight check for state: %s", coresight_state_to_string(state));

	spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);

	list_for_each_entry(config_entry, &kbdev->csf.coresight.configs, link) {
		if (state != config_entry->state) {
			success = false;
			break;
		}
	}

	spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);

	return success;
}
KBASE_EXPORT_TEST_API(kbase_debug_coresight_csf_state_check);

bool kbase_debug_coresight_csf_state_wait(struct kbase_device *kbdev,
					  enum kbase_debug_coresight_csf_state state)
{
	const long wait_timeout = kbase_csf_timeout_in_jiffies(kbdev->csf.fw_timeout_ms);
	struct kbase_debug_coresight_csf_config *config_entry, *next_config_entry;
	unsigned long flags;
	bool success = true;

	dev_dbg(kbdev->dev, "Coresight wait for state: %s", coresight_state_to_string(state));

	spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);

	list_for_each_entry_safe(config_entry, next_config_entry, &kbdev->csf.coresight.configs,
				  link) {
		const enum kbase_debug_coresight_csf_state prev_state = config_entry->state;
		long remaining;

		spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);
		remaining = wait_event_timeout(kbdev->csf.coresight.event_wait,
					       state == config_entry->state, wait_timeout);
		spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);

		if (!remaining) {
			success = false;
			dev_err(kbdev->dev,
				"Timeout waiting for Coresight state transition %s to %s",
				coresight_state_to_string(prev_state),
				coresight_state_to_string(state));
		}
	}

	spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);

	return success;
}
KBASE_EXPORT_TEST_API(kbase_debug_coresight_csf_state_wait);

int kbase_debug_coresight_csf_init(struct kbase_device *kbdev)
{
	kbdev->csf.coresight.workq = alloc_ordered_workqueue("Mali CoreSight workqueue", 0);
	if (kbdev->csf.coresight.workq == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&kbdev->csf.coresight.clients);
	INIT_LIST_HEAD(&kbdev->csf.coresight.configs);
	INIT_WORK(&kbdev->csf.coresight.enable_work, coresight_config_enable_all);
	INIT_WORK(&kbdev->csf.coresight.disable_work, coresight_config_disable_all);
	init_waitqueue_head(&kbdev->csf.coresight.event_wait);
	spin_lock_init(&kbdev->csf.coresight.lock);

	kbdev->csf.coresight.disable_on_pmode_enter = false;
	kbdev->csf.coresight.enable_on_pmode_exit = false;

	return 0;
}

void kbase_debug_coresight_csf_term(struct kbase_device *kbdev)
{
	struct kbase_debug_coresight_csf_client *client_entry, *next_client_entry;
	struct kbase_debug_coresight_csf_config *config_entry, *next_config_entry;
	unsigned long flags;

	kbdev->csf.coresight.disable_on_pmode_enter = false;
	kbdev->csf.coresight.enable_on_pmode_exit = false;

	cancel_work_sync(&kbdev->csf.coresight.enable_work);
	cancel_work_sync(&kbdev->csf.coresight.disable_work);
	destroy_workqueue(kbdev->csf.coresight.workq);
	kbdev->csf.coresight.workq = NULL;

	spin_lock_irqsave(&kbdev->csf.coresight.lock, flags);

	list_for_each_entry_safe(config_entry, next_config_entry, &kbdev->csf.coresight.configs,
				  link) {
		list_del_init(&config_entry->link);
		kfree(config_entry);
	}

	list_for_each_entry_safe(client_entry, next_client_entry, &kbdev->csf.coresight.clients,
				  link) {
		list_del_init(&client_entry->link);
		kfree(client_entry);
	}

	spin_unlock_irqrestore(&kbdev->csf.coresight.lock, flags);
}
