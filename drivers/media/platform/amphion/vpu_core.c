// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>
#include "vpu.h"
#include "vpu_defs.h"
#include "vpu_core.h"
#include "vpu_mbox.h"
#include "vpu_msgs.h"
#include "vpu_rpc.h"
#include "vpu_cmds.h"

void csr_writel(struct vpu_core *core, u32 reg, u32 val)
{
	writel(val, core->base + reg);
}

u32 csr_readl(struct vpu_core *core, u32 reg)
{
	return readl(core->base + reg);
}

static int vpu_core_load_firmware(struct vpu_core *core)
{
	const struct firmware *pfw = NULL;
	int ret = 0;

	if (!core->fw.virt) {
		dev_err(core->dev, "firmware buffer is not ready\n");
		return -EINVAL;
	}

	ret = request_firmware(&pfw, core->res->fwname, core->dev);
	dev_dbg(core->dev, "request_firmware %s : %d\n", core->res->fwname, ret);
	if (ret) {
		dev_err(core->dev, "request firmware %s failed, ret = %d\n",
			core->res->fwname, ret);
		return ret;
	}

	if (core->fw.length < pfw->size) {
		dev_err(core->dev, "firmware buffer size want %zu, but %d\n",
			pfw->size, core->fw.length);
		ret = -EINVAL;
		goto exit;
	}

	memset(core->fw.virt, 0, core->fw.length);
	memcpy(core->fw.virt, pfw->data, pfw->size);
	core->fw.bytesused = pfw->size;
	ret = vpu_iface_on_firmware_loaded(core);
exit:
	release_firmware(pfw);
	pfw = NULL;

	return ret;
}

static int vpu_core_boot_done(struct vpu_core *core)
{
	u32 fw_version;

	fw_version = vpu_iface_get_version(core);
	dev_info(core->dev, "%s firmware version : %d.%d.%d\n",
		 vpu_core_type_desc(core->type),
		 (fw_version >> 16) & 0xff,
		 (fw_version >> 8) & 0xff,
		 fw_version & 0xff);
	core->supported_instance_count = vpu_iface_get_max_instance_count(core);
	if (core->res->act_size) {
		u32 count = core->act.length / core->res->act_size;

		core->supported_instance_count = min(core->supported_instance_count, count);
	}
	core->fw_version = fw_version;
	vpu_core_set_state(core, VPU_CORE_ACTIVE);

	return 0;
}

static int vpu_core_wait_boot_done(struct vpu_core *core)
{
	int ret;

	ret = wait_for_completion_timeout(&core->cmp, VPU_TIMEOUT);
	if (!ret) {
		dev_err(core->dev, "boot timeout\n");
		return -EINVAL;
	}
	return vpu_core_boot_done(core);
}

static int vpu_core_boot(struct vpu_core *core, bool load)
{
	int ret;

	reinit_completion(&core->cmp);
	if (load) {
		ret = vpu_core_load_firmware(core);
		if (ret)
			return ret;
	}

	vpu_iface_boot_core(core);
	return vpu_core_wait_boot_done(core);
}

static int vpu_core_shutdown(struct vpu_core *core)
{
	return vpu_iface_shutdown_core(core);
}

static int vpu_core_restore(struct vpu_core *core)
{
	int ret;

	ret = vpu_core_sw_reset(core);
	if (ret)
		return ret;

	vpu_core_boot_done(core);
	return vpu_iface_restore_core(core);
}

static int __vpu_alloc_dma(struct device *dev, struct vpu_buffer *buf)
{
	gfp_t gfp = GFP_KERNEL | GFP_DMA32;

	if (!buf->length)
		return 0;

	buf->virt = dma_alloc_coherent(dev, buf->length, &buf->phys, gfp);
	if (!buf->virt)
		return -ENOMEM;

	buf->dev = dev;

	return 0;
}

void vpu_free_dma(struct vpu_buffer *buf)
{
	if (!buf->virt || !buf->dev)
		return;

	dma_free_coherent(buf->dev, buf->length, buf->virt, buf->phys);
	buf->virt = NULL;
	buf->phys = 0;
	buf->length = 0;
	buf->bytesused = 0;
	buf->dev = NULL;
}

int vpu_alloc_dma(struct vpu_core *core, struct vpu_buffer *buf)
{
	return __vpu_alloc_dma(core->dev, buf);
}

void vpu_core_set_state(struct vpu_core *core, enum vpu_core_state state)
{
	if (state != core->state)
		vpu_trace(core->dev, "vpu core state change from %d to %d\n", core->state, state);
	core->state = state;
	if (core->state == VPU_CORE_DEINIT)
		core->hang_mask = 0;
}

static void vpu_core_update_state(struct vpu_core *core)
{
	if (!vpu_iface_get_power_state(core)) {
		if (core->request_count)
			vpu_core_set_state(core, VPU_CORE_HANG);
		else
			vpu_core_set_state(core, VPU_CORE_DEINIT);

	} else if (core->state == VPU_CORE_ACTIVE && core->hang_mask) {
		vpu_core_set_state(core, VPU_CORE_HANG);
	}
}

static struct vpu_core *vpu_core_find_proper_by_type(struct vpu_dev *vpu, u32 type)
{
	struct vpu_core *core = NULL;
	int request_count = INT_MAX;
	struct vpu_core *c;

	list_for_each_entry(c, &vpu->cores, list) {
		dev_dbg(c->dev, "instance_mask = 0x%lx, state = %d\n", c->instance_mask, c->state);
		if (c->type != type)
			continue;
		mutex_lock(&c->lock);
		vpu_core_update_state(c);
		mutex_unlock(&c->lock);
		if (c->state == VPU_CORE_DEINIT) {
			core = c;
			break;
		}
		if (c->state != VPU_CORE_ACTIVE)
			continue;
		if (c->request_count < request_count) {
			request_count = c->request_count;
			core = c;
		}
		if (!request_count)
			break;
	}

	return core;
}

static bool vpu_core_is_exist(struct vpu_dev *vpu, struct vpu_core *core)
{
	struct vpu_core *c;

	list_for_each_entry(c, &vpu->cores, list) {
		if (c == core)
			return true;
	}

	return false;
}

static void vpu_core_get_vpu(struct vpu_core *core)
{
	core->vpu->get_vpu(core->vpu);
	if (core->type == VPU_CORE_TYPE_ENC)
		core->vpu->get_enc(core->vpu);
	if (core->type == VPU_CORE_TYPE_DEC)
		core->vpu->get_dec(core->vpu);
}

static int vpu_core_register(struct device *dev, struct vpu_core *core)
{
	struct vpu_dev *vpu = dev_get_drvdata(dev);
	int ret = 0;

	dev_dbg(core->dev, "register core %s\n", vpu_core_type_desc(core->type));
	if (vpu_core_is_exist(vpu, core))
		return 0;

	core->workqueue = alloc_ordered_workqueue("vpu", WQ_MEM_RECLAIM);
	if (!core->workqueue) {
		dev_err(core->dev, "fail to alloc workqueue\n");
		return -ENOMEM;
	}
	INIT_WORK(&core->msg_work, vpu_msg_run_work);
	INIT_DELAYED_WORK(&core->msg_delayed_work, vpu_msg_delayed_work);
	core->msg_buffer_size = roundup_pow_of_two(VPU_MSG_BUFFER_SIZE);
	core->msg_buffer = vzalloc(core->msg_buffer_size);
	if (!core->msg_buffer) {
		dev_err(core->dev, "failed allocate buffer for fifo\n");
		ret = -ENOMEM;
		goto error;
	}
	ret = kfifo_init(&core->msg_fifo, core->msg_buffer, core->msg_buffer_size);
	if (ret) {
		dev_err(core->dev, "failed init kfifo\n");
		goto error;
	}

	list_add_tail(&core->list, &vpu->cores);
	vpu_core_get_vpu(core);

	return 0;
error:
	if (core->msg_buffer) {
		vfree(core->msg_buffer);
		core->msg_buffer = NULL;
	}
	if (core->workqueue) {
		destroy_workqueue(core->workqueue);
		core->workqueue = NULL;
	}
	return ret;
}

static void vpu_core_put_vpu(struct vpu_core *core)
{
	if (core->type == VPU_CORE_TYPE_ENC)
		core->vpu->put_enc(core->vpu);
	if (core->type == VPU_CORE_TYPE_DEC)
		core->vpu->put_dec(core->vpu);
	core->vpu->put_vpu(core->vpu);
}

static int vpu_core_unregister(struct device *dev, struct vpu_core *core)
{
	list_del_init(&core->list);

	vpu_core_put_vpu(core);
	core->vpu = NULL;
	vfree(core->msg_buffer);
	core->msg_buffer = NULL;

	if (core->workqueue) {
		cancel_work_sync(&core->msg_work);
		cancel_delayed_work_sync(&core->msg_delayed_work);
		destroy_workqueue(core->workqueue);
		core->workqueue = NULL;
	}

	return 0;
}

static int vpu_core_acquire_instance(struct vpu_core *core)
{
	int id;

	id = ffz(core->instance_mask);
	if (id >= core->supported_instance_count)
		return -EINVAL;

	set_bit(id, &core->instance_mask);

	return id;
}

static void vpu_core_release_instance(struct vpu_core *core, int id)
{
	if (id < 0 || id >= core->supported_instance_count)
		return;

	clear_bit(id, &core->instance_mask);
}

struct vpu_inst *vpu_inst_get(struct vpu_inst *inst)
{
	if (!inst)
		return NULL;

	atomic_inc(&inst->ref_count);

	return inst;
}

void vpu_inst_put(struct vpu_inst *inst)
{
	if (!inst)
		return;
	if (atomic_dec_and_test(&inst->ref_count)) {
		if (inst->release)
			inst->release(inst);
	}
}

struct vpu_core *vpu_request_core(struct vpu_dev *vpu, enum vpu_core_type type)
{
	struct vpu_core *core = NULL;
	int ret;

	mutex_lock(&vpu->lock);

	core = vpu_core_find_proper_by_type(vpu, type);
	if (!core)
		goto exit;

	mutex_lock(&core->lock);
	pm_runtime_resume_and_get(core->dev);

	if (core->state == VPU_CORE_DEINIT) {
		if (vpu_iface_get_power_state(core))
			ret = vpu_core_restore(core);
		else
			ret = vpu_core_boot(core, true);
		if (ret) {
			pm_runtime_put_sync(core->dev);
			mutex_unlock(&core->lock);
			core = NULL;
			goto exit;
		}
	}

	core->request_count++;

	mutex_unlock(&core->lock);
exit:
	mutex_unlock(&vpu->lock);

	return core;
}

void vpu_release_core(struct vpu_core *core)
{
	if (!core)
		return;

	mutex_lock(&core->lock);
	pm_runtime_put_sync(core->dev);
	if (core->request_count)
		core->request_count--;
	mutex_unlock(&core->lock);
}

int vpu_inst_register(struct vpu_inst *inst)
{
	struct vpu_dev *vpu;
	struct vpu_core *core;
	int ret = 0;

	vpu = inst->vpu;
	core = inst->core;
	if (!core) {
		core = vpu_request_core(vpu, inst->type);
		if (!core) {
			dev_err(vpu->dev, "there is no vpu core for %s\n",
				vpu_core_type_desc(inst->type));
			return -EINVAL;
		}
		inst->core = core;
		inst->dev = get_device(core->dev);
	}

	mutex_lock(&core->lock);
	if (core->state != VPU_CORE_ACTIVE) {
		dev_err(core->dev, "vpu core is not active, state = %d\n", core->state);
		ret = -EINVAL;
		goto exit;
	}

	if (inst->id >= 0 && inst->id < core->supported_instance_count)
		goto exit;

	ret = vpu_core_acquire_instance(core);
	if (ret < 0)
		goto exit;

	vpu_trace(inst->dev, "[%d] %p\n", ret, inst);
	inst->id = ret;
	list_add_tail(&inst->list, &core->instances);
	ret = 0;
	if (core->res->act_size) {
		inst->act.phys = core->act.phys + core->res->act_size * inst->id;
		inst->act.virt = core->act.virt + core->res->act_size * inst->id;
		inst->act.length = core->res->act_size;
	}
	vpu_inst_create_dbgfs_file(inst);
exit:
	mutex_unlock(&core->lock);

	if (ret)
		dev_err(core->dev, "register instance fail\n");
	return ret;
}

int vpu_inst_unregister(struct vpu_inst *inst)
{
	struct vpu_core *core;

	if (!inst->core)
		return 0;

	core = inst->core;
	vpu_clear_request(inst);
	mutex_lock(&core->lock);
	if (inst->id >= 0 && inst->id < core->supported_instance_count) {
		vpu_inst_remove_dbgfs_file(inst);
		list_del_init(&inst->list);
		vpu_core_release_instance(core, inst->id);
		inst->id = VPU_INST_NULL_ID;
	}
	vpu_core_update_state(core);
	if (core->state == VPU_CORE_HANG && !core->instance_mask) {
		int err;

		dev_info(core->dev, "reset hang core\n");
		mutex_unlock(&core->lock);
		err = vpu_core_sw_reset(core);
		mutex_lock(&core->lock);
		if (!err) {
			vpu_core_set_state(core, VPU_CORE_ACTIVE);
			core->hang_mask = 0;
		}
	}
	mutex_unlock(&core->lock);

	return 0;
}

struct vpu_inst *vpu_core_find_instance(struct vpu_core *core, u32 index)
{
	struct vpu_inst *inst = NULL;
	struct vpu_inst *tmp;

	mutex_lock(&core->lock);
	if (index >= core->supported_instance_count || !test_bit(index, &core->instance_mask))
		goto exit;
	list_for_each_entry(tmp, &core->instances, list) {
		if (tmp->id == index) {
			inst = vpu_inst_get(tmp);
			break;
		}
	}
exit:
	mutex_unlock(&core->lock);

	return inst;
}

const struct vpu_core_resources *vpu_get_resource(struct vpu_inst *inst)
{
	struct vpu_dev *vpu;
	struct vpu_core *core = NULL;
	const struct vpu_core_resources *res = NULL;

	if (!inst || !inst->vpu)
		return NULL;

	if (inst->core && inst->core->res)
		return inst->core->res;

	vpu = inst->vpu;
	mutex_lock(&vpu->lock);
	list_for_each_entry(core, &vpu->cores, list) {
		if (core->type == inst->type) {
			res = core->res;
			break;
		}
	}
	mutex_unlock(&vpu->lock);

	return res;
}

static int vpu_core_parse_dt(struct vpu_core *core, struct device_node *np)
{
	struct device_node *node;
	struct resource res;
	int ret;

	if (of_count_phandle_with_args(np, "memory-region", NULL) < 2) {
		dev_err(core->dev, "need 2 memory-region for boot and rpc\n");
		return -ENODEV;
	}

	node = of_parse_phandle(np, "memory-region", 0);
	if (!node) {
		dev_err(core->dev, "boot-region of_parse_phandle error\n");
		return -ENODEV;
	}
	if (of_address_to_resource(node, 0, &res)) {
		dev_err(core->dev, "boot-region of_address_to_resource error\n");
		of_node_put(node);
		return -EINVAL;
	}
	core->fw.phys = res.start;
	core->fw.length = resource_size(&res);

	of_node_put(node);

	node = of_parse_phandle(np, "memory-region", 1);
	if (!node) {
		dev_err(core->dev, "rpc-region of_parse_phandle error\n");
		return -ENODEV;
	}
	if (of_address_to_resource(node, 0, &res)) {
		dev_err(core->dev, "rpc-region of_address_to_resource error\n");
		of_node_put(node);
		return -EINVAL;
	}
	core->rpc.phys = res.start;
	core->rpc.length = resource_size(&res);

	if (core->rpc.length < core->res->rpc_size + core->res->fwlog_size) {
		dev_err(core->dev, "the rpc-region <%pad, 0x%x> is not enough\n",
			&core->rpc.phys, core->rpc.length);
		of_node_put(node);
		return -EINVAL;
	}

	core->fw.virt = memremap(core->fw.phys, core->fw.length, MEMREMAP_WC);
	core->rpc.virt = memremap(core->rpc.phys, core->rpc.length, MEMREMAP_WC);
	memset(core->rpc.virt, 0, core->rpc.length);

	ret = vpu_iface_check_memory_region(core, core->rpc.phys, core->rpc.length);
	if (ret != VPU_CORE_MEMORY_UNCACHED) {
		dev_err(core->dev, "rpc region<%pad, 0x%x> isn't uncached\n",
			&core->rpc.phys, core->rpc.length);
		of_node_put(node);
		return -EINVAL;
	}

	core->log.phys = core->rpc.phys + core->res->rpc_size;
	core->log.virt = core->rpc.virt + core->res->rpc_size;
	core->log.length = core->res->fwlog_size;
	core->act.phys = core->log.phys + core->log.length;
	core->act.virt = core->log.virt + core->log.length;
	core->act.length = core->rpc.length - core->res->rpc_size - core->log.length;
	core->rpc.length = core->res->rpc_size;

	of_node_put(node);

	return 0;
}

static int vpu_core_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vpu_core *core;
	struct vpu_dev *vpu = dev_get_drvdata(dev->parent);
	struct vpu_shared_addr *iface;
	u32 iface_data_size;
	int ret;

	dev_dbg(dev, "probe\n");
	if (!vpu)
		return -EINVAL;
	core = devm_kzalloc(dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->pdev = pdev;
	core->dev = dev;
	platform_set_drvdata(pdev, core);
	core->vpu = vpu;
	INIT_LIST_HEAD(&core->instances);
	mutex_init(&core->lock);
	mutex_init(&core->cmd_lock);
	init_completion(&core->cmp);
	init_waitqueue_head(&core->ack_wq);
	vpu_core_set_state(core, VPU_CORE_DEINIT);

	core->res = of_device_get_match_data(dev);
	if (!core->res)
		return -ENODEV;

	core->type = core->res->type;
	core->id = of_alias_get_id(dev->of_node, "vpu_core");
	if (core->id < 0) {
		dev_err(dev, "can't get vpu core id\n");
		return core->id;
	}
	dev_info(core->dev, "[%d] = %s\n", core->id, vpu_core_type_desc(core->type));
	ret = vpu_core_parse_dt(core, dev->of_node);
	if (ret)
		return ret;

	core->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(core->base))
		return PTR_ERR(core->base);

	if (!vpu_iface_check_codec(core)) {
		dev_err(core->dev, "is not supported\n");
		return -EINVAL;
	}

	ret = vpu_mbox_init(core);
	if (ret)
		return ret;

	iface = devm_kzalloc(dev, sizeof(*iface), GFP_KERNEL);
	if (!iface)
		return -ENOMEM;

	iface_data_size = vpu_iface_get_data_size(core);
	if (iface_data_size) {
		iface->priv = devm_kzalloc(dev, iface_data_size, GFP_KERNEL);
		if (!iface->priv)
			return -ENOMEM;
	}

	ret = vpu_iface_init(core, iface, &core->rpc, core->fw.phys);
	if (ret) {
		dev_err(core->dev, "init iface fail, ret = %d\n", ret);
		return ret;
	}

	vpu_iface_config_system(core, vpu->res->mreg_base, vpu->base);
	vpu_iface_set_log_buf(core, &core->log);

	pm_runtime_enable(dev);
	ret = pm_runtime_resume_and_get(dev);
	if (ret) {
		pm_runtime_put_noidle(dev);
		pm_runtime_set_suspended(dev);
		goto err_runtime_disable;
	}

	ret = vpu_core_register(dev->parent, core);
	if (ret)
		goto err_core_register;
	core->parent = dev->parent;

	pm_runtime_put_sync(dev);
	vpu_core_create_dbgfs_file(core);

	return 0;

err_core_register:
	pm_runtime_put_sync(dev);
err_runtime_disable:
	pm_runtime_disable(dev);

	return ret;
}

static void vpu_core_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct vpu_core *core = platform_get_drvdata(pdev);
	int ret;

	vpu_core_remove_dbgfs_file(core);
	ret = pm_runtime_resume_and_get(dev);
	WARN_ON(ret < 0);

	vpu_core_shutdown(core);
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	vpu_core_unregister(core->parent, core);
	memunmap(core->fw.virt);
	memunmap(core->rpc.virt);
	mutex_destroy(&core->lock);
	mutex_destroy(&core->cmd_lock);
}

static int __maybe_unused vpu_core_runtime_resume(struct device *dev)
{
	struct vpu_core *core = dev_get_drvdata(dev);

	return vpu_mbox_request(core);
}

static int __maybe_unused vpu_core_runtime_suspend(struct device *dev)
{
	struct vpu_core *core = dev_get_drvdata(dev);

	vpu_mbox_free(core);
	return 0;
}

static void vpu_core_cancel_work(struct vpu_core *core)
{
	struct vpu_inst *inst = NULL;

	cancel_work_sync(&core->msg_work);
	cancel_delayed_work_sync(&core->msg_delayed_work);

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list)
		cancel_work_sync(&inst->msg_work);
	mutex_unlock(&core->lock);
}

static void vpu_core_resume_work(struct vpu_core *core)
{
	struct vpu_inst *inst = NULL;
	unsigned long delay = msecs_to_jiffies(10);

	queue_work(core->workqueue, &core->msg_work);
	queue_delayed_work(core->workqueue, &core->msg_delayed_work, delay);

	mutex_lock(&core->lock);
	list_for_each_entry(inst, &core->instances, list)
		queue_work(inst->workqueue, &inst->msg_work);
	mutex_unlock(&core->lock);
}

static int __maybe_unused vpu_core_resume(struct device *dev)
{
	struct vpu_core *core = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&core->lock);
	pm_runtime_resume_and_get(dev);
	vpu_core_get_vpu(core);

	if (core->request_count) {
		if (!vpu_iface_get_power_state(core))
			ret = vpu_core_boot(core, false);
		else
			ret = vpu_core_sw_reset(core);
		if (ret) {
			dev_err(core->dev, "resume fail\n");
			vpu_core_set_state(core, VPU_CORE_HANG);
		}
	}
	vpu_core_update_state(core);
	pm_runtime_put_sync(dev);
	mutex_unlock(&core->lock);

	vpu_core_resume_work(core);
	return ret;
}

static int __maybe_unused vpu_core_suspend(struct device *dev)
{
	struct vpu_core *core = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&core->lock);
	if (core->request_count)
		ret = vpu_core_snapshot(core);
	mutex_unlock(&core->lock);
	if (ret)
		return ret;

	vpu_core_cancel_work(core);

	mutex_lock(&core->lock);
	vpu_core_put_vpu(core);
	mutex_unlock(&core->lock);
	return ret;
}

static const struct dev_pm_ops vpu_core_pm_ops = {
	SET_RUNTIME_PM_OPS(vpu_core_runtime_suspend, vpu_core_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(vpu_core_suspend, vpu_core_resume)
};

static struct vpu_core_resources imx8q_enc = {
	.type = VPU_CORE_TYPE_ENC,
	.fwname = "amphion/vpu/vpu_fw_imx8_enc.bin",
	.stride = 16,
	.max_width = 1920,
	.max_height = 1920,
	.min_width = 64,
	.min_height = 48,
	.step_width = 2,
	.step_height = 2,
	.rpc_size = 0x80000,
	.fwlog_size = 0x80000,
	.act_size = 0xc0000,
};

static struct vpu_core_resources imx8q_dec = {
	.type = VPU_CORE_TYPE_DEC,
	.fwname = "amphion/vpu/vpu_fw_imx8_dec.bin",
	.stride = 256,
	.max_width = 8188,
	.max_height = 8188,
	.min_width = 16,
	.min_height = 16,
	.step_width = 1,
	.step_height = 1,
	.rpc_size = 0x80000,
	.fwlog_size = 0x80000,
};

static const struct of_device_id vpu_core_dt_match[] = {
	{ .compatible = "nxp,imx8q-vpu-encoder", .data = &imx8q_enc },
	{ .compatible = "nxp,imx8q-vpu-decoder", .data = &imx8q_dec },
	{}
};
MODULE_DEVICE_TABLE(of, vpu_core_dt_match);

static struct platform_driver amphion_vpu_core_driver = {
	.probe = vpu_core_probe,
	.remove_new = vpu_core_remove,
	.driver = {
		.name = "amphion-vpu-core",
		.of_match_table = vpu_core_dt_match,
		.pm = &vpu_core_pm_ops,
	},
};

int __init vpu_core_driver_init(void)
{
	return platform_driver_register(&amphion_vpu_core_driver);
}

void __exit vpu_core_driver_exit(void)
{
	platform_driver_unregister(&amphion_vpu_core_driver);
}
