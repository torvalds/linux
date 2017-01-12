/*
 *
 * (C) COPYRIGHT 2014-2016 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <mali_kbase.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include <backend/gpu/mali_kbase_irq_internal.h>

#include <linux/interrupt.h>

#if !defined(CONFIG_MALI_NO_MALI)

/* GPU IRQ Tags */
#define	JOB_IRQ_TAG	0
#define MMU_IRQ_TAG	1
#define GPU_IRQ_TAG	2

static void *kbase_tag(void *ptr, u32 tag)
{
	return (void *)(((uintptr_t) ptr) | tag);
}

static void *kbase_untag(void *ptr)
{
	return (void *)(((uintptr_t) ptr) & ~3);
}

static irqreturn_t kbase_job_irq_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (!kbdev->pm.backend.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock,
									flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_STATUS), NULL);

#ifdef CONFIG_MALI_DEBUG
	if (!kbdev->pm.backend.driver_ready_for_irqs)
		dev_warn(kbdev->dev, "%s: irq %d irqstatus 0x%x before driver is ready\n",
				__func__, irq, val);
#endif /* CONFIG_MALI_DEBUG */
	spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbase_job_done(kbdev, val);

	return IRQ_HANDLED;
}

KBASE_EXPORT_TEST_API(kbase_job_irq_handler);

static irqreturn_t kbase_mmu_irq_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (!kbdev->pm.backend.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock,
									flags);
		return IRQ_NONE;
	}

	atomic_inc(&kbdev->faults_pending);

	val = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_STATUS), NULL);

#ifdef CONFIG_MALI_DEBUG
	if (!kbdev->pm.backend.driver_ready_for_irqs)
		dev_warn(kbdev->dev, "%s: irq %d irqstatus 0x%x before driver is ready\n",
				__func__, irq, val);
#endif /* CONFIG_MALI_DEBUG */
	spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (!val) {
		atomic_dec(&kbdev->faults_pending);
		return IRQ_NONE;
	}

	dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbase_mmu_interrupt(kbdev, val);

	atomic_dec(&kbdev->faults_pending);

	return IRQ_HANDLED;
}

static irqreturn_t kbase_gpu_irq_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (!kbdev->pm.backend.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock,
									flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_STATUS), NULL);

#ifdef CONFIG_MALI_DEBUG
	if (!kbdev->pm.backend.driver_ready_for_irqs)
		dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x before driver is ready\n",
				__func__, irq, val);
#endif /* CONFIG_MALI_DEBUG */
	spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbase_gpu_interrupt(kbdev, val);

	return IRQ_HANDLED;
}

static irq_handler_t kbase_handler_table[] = {
	[JOB_IRQ_TAG] = kbase_job_irq_handler,
	[MMU_IRQ_TAG] = kbase_mmu_irq_handler,
	[GPU_IRQ_TAG] = kbase_gpu_irq_handler,
};

#ifdef CONFIG_MALI_DEBUG
#define  JOB_IRQ_HANDLER JOB_IRQ_TAG
#define  MMU_IRQ_HANDLER MMU_IRQ_TAG
#define  GPU_IRQ_HANDLER GPU_IRQ_TAG

/**
 * kbase_set_custom_irq_handler - Set a custom IRQ handler
 * @kbdev: Device for which the handler is to be registered
 * @custom_handler: Handler to be registered
 * @irq_type: Interrupt type
 *
 * Registers given interrupt handler for requested interrupt type
 * In the case where irq handler is not specified, the default handler shall be
 * registered
 *
 * Return: 0 case success, error code otherwise
 */
int kbase_set_custom_irq_handler(struct kbase_device *kbdev,
					irq_handler_t custom_handler,
					int irq_type)
{
	int result = 0;
	irq_handler_t requested_irq_handler = NULL;

	KBASE_DEBUG_ASSERT((JOB_IRQ_HANDLER <= irq_type) &&
						(GPU_IRQ_HANDLER >= irq_type));

	/* Release previous handler */
	if (kbdev->irqs[irq_type].irq)
		free_irq(kbdev->irqs[irq_type].irq, kbase_tag(kbdev, irq_type));

	requested_irq_handler = (NULL != custom_handler) ? custom_handler :
						kbase_handler_table[irq_type];

	if (0 != request_irq(kbdev->irqs[irq_type].irq,
			requested_irq_handler,
			kbdev->irqs[irq_type].flags | IRQF_SHARED,
			dev_name(kbdev->dev), kbase_tag(kbdev, irq_type))) {
		result = -EINVAL;
		dev_err(kbdev->dev, "Can't request interrupt %d (index %d)\n",
					kbdev->irqs[irq_type].irq, irq_type);
#ifdef CONFIG_SPARSE_IRQ
		dev_err(kbdev->dev, "You have CONFIG_SPARSE_IRQ support enabled - is the interrupt number correct for this configuration?\n");
#endif /* CONFIG_SPARSE_IRQ */
	}

	return result;
}

KBASE_EXPORT_TEST_API(kbase_set_custom_irq_handler);

/* test correct interrupt assigment and reception by cpu */
struct kbasep_irq_test {
	struct hrtimer timer;
	wait_queue_head_t wait;
	int triggered;
	u32 timeout;
};

static struct kbasep_irq_test kbasep_irq_test_data;

#define IRQ_TEST_TIMEOUT    500

static irqreturn_t kbase_job_irq_test_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (!kbdev->pm.backend.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock,
									flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_STATUS), NULL);

	spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbasep_irq_test_data.triggered = 1;
	wake_up(&kbasep_irq_test_data.wait);

	kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR), val, NULL);

	return IRQ_HANDLED;
}

static irqreturn_t kbase_mmu_irq_test_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (!kbdev->pm.backend.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock,
									flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_STATUS), NULL);

	spin_unlock_irqrestore(&kbdev->pm.backend.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbasep_irq_test_data.triggered = 1;
	wake_up(&kbasep_irq_test_data.wait);

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), val, NULL);

	return IRQ_HANDLED;
}

static enum hrtimer_restart kbasep_test_interrupt_timeout(struct hrtimer *timer)
{
	struct kbasep_irq_test *test_data = container_of(timer,
						struct kbasep_irq_test, timer);

	test_data->timeout = 1;
	test_data->triggered = 1;
	wake_up(&test_data->wait);
	return HRTIMER_NORESTART;
}

static int kbasep_common_test_interrupt(
				struct kbase_device * const kbdev, u32 tag)
{
	int err = 0;
	irq_handler_t test_handler;

	u32 old_mask_val;
	u16 mask_offset;
	u16 rawstat_offset;

	switch (tag) {
	case JOB_IRQ_TAG:
		test_handler = kbase_job_irq_test_handler;
		rawstat_offset = JOB_CONTROL_REG(JOB_IRQ_RAWSTAT);
		mask_offset = JOB_CONTROL_REG(JOB_IRQ_MASK);
		break;
	case MMU_IRQ_TAG:
		test_handler = kbase_mmu_irq_test_handler;
		rawstat_offset = MMU_REG(MMU_IRQ_RAWSTAT);
		mask_offset = MMU_REG(MMU_IRQ_MASK);
		break;
	case GPU_IRQ_TAG:
		/* already tested by pm_driver - bail out */
	default:
		return 0;
	}

	/* store old mask */
	old_mask_val = kbase_reg_read(kbdev, mask_offset, NULL);
	/* mask interrupts */
	kbase_reg_write(kbdev, mask_offset, 0x0, NULL);

	if (kbdev->irqs[tag].irq) {
		/* release original handler and install test handler */
		if (kbase_set_custom_irq_handler(kbdev, test_handler, tag) != 0) {
			err = -EINVAL;
		} else {
			kbasep_irq_test_data.timeout = 0;
			hrtimer_init(&kbasep_irq_test_data.timer,
					CLOCK_MONOTONIC, HRTIMER_MODE_REL);
			kbasep_irq_test_data.timer.function =
						kbasep_test_interrupt_timeout;

			/* trigger interrupt */
			kbase_reg_write(kbdev, mask_offset, 0x1, NULL);
			kbase_reg_write(kbdev, rawstat_offset, 0x1, NULL);

			hrtimer_start(&kbasep_irq_test_data.timer,
					HR_TIMER_DELAY_MSEC(IRQ_TEST_TIMEOUT),
					HRTIMER_MODE_REL);

			wait_event(kbasep_irq_test_data.wait,
					kbasep_irq_test_data.triggered != 0);

			if (kbasep_irq_test_data.timeout != 0) {
				dev_err(kbdev->dev, "Interrupt %d (index %d) didn't reach CPU.\n",
						kbdev->irqs[tag].irq, tag);
				err = -EINVAL;
			} else {
				dev_dbg(kbdev->dev, "Interrupt %d (index %d) reached CPU.\n",
						kbdev->irqs[tag].irq, tag);
			}

			hrtimer_cancel(&kbasep_irq_test_data.timer);
			kbasep_irq_test_data.triggered = 0;

			/* mask interrupts */
			kbase_reg_write(kbdev, mask_offset, 0x0, NULL);

			/* release test handler */
			free_irq(kbdev->irqs[tag].irq, kbase_tag(kbdev, tag));
		}

		/* restore original interrupt */
		if (request_irq(kbdev->irqs[tag].irq, kbase_handler_table[tag],
				kbdev->irqs[tag].flags | IRQF_SHARED,
				dev_name(kbdev->dev), kbase_tag(kbdev, tag))) {
			dev_err(kbdev->dev, "Can't restore original interrupt %d (index %d)\n",
						kbdev->irqs[tag].irq, tag);
			err = -EINVAL;
		}
	}
	/* restore old mask */
	kbase_reg_write(kbdev, mask_offset, old_mask_val, NULL);

	return err;
}

int kbasep_common_test_interrupt_handlers(
					struct kbase_device * const kbdev)
{
	int err;

	init_waitqueue_head(&kbasep_irq_test_data.wait);
	kbasep_irq_test_data.triggered = 0;

	/* A suspend won't happen during startup/insmod */
	kbase_pm_context_active(kbdev);

	err = kbasep_common_test_interrupt(kbdev, JOB_IRQ_TAG);
	if (err) {
		dev_err(kbdev->dev, "Interrupt JOB_IRQ didn't reach CPU. Check interrupt assignments.\n");
		goto out;
	}

	err = kbasep_common_test_interrupt(kbdev, MMU_IRQ_TAG);
	if (err) {
		dev_err(kbdev->dev, "Interrupt MMU_IRQ didn't reach CPU. Check interrupt assignments.\n");
		goto out;
	}

	dev_dbg(kbdev->dev, "Interrupts are correctly assigned.\n");

 out:
	kbase_pm_context_idle(kbdev);

	return err;
}
#endif /* CONFIG_MALI_DEBUG */

int kbase_install_interrupts(struct kbase_device *kbdev)
{
	u32 nr = ARRAY_SIZE(kbase_handler_table);
	int err;
	u32 i;

	for (i = 0; i < nr; i++) {
		err = request_irq(kbdev->irqs[i].irq, kbase_handler_table[i],
				kbdev->irqs[i].flags | IRQF_SHARED,
				dev_name(kbdev->dev),
				kbase_tag(kbdev, i));
		if (err) {
			dev_err(kbdev->dev, "Can't request interrupt %d (index %d)\n",
							kbdev->irqs[i].irq, i);
#ifdef CONFIG_SPARSE_IRQ
			dev_err(kbdev->dev, "You have CONFIG_SPARSE_IRQ support enabled - is the interrupt number correct for this configuration?\n");
#endif /* CONFIG_SPARSE_IRQ */
			goto release;
		}
	}

	return 0;

 release:
	while (i-- > 0)
		free_irq(kbdev->irqs[i].irq, kbase_tag(kbdev, i));

	return err;
}

void kbase_release_interrupts(struct kbase_device *kbdev)
{
	u32 nr = ARRAY_SIZE(kbase_handler_table);
	u32 i;

	for (i = 0; i < nr; i++) {
		if (kbdev->irqs[i].irq)
			free_irq(kbdev->irqs[i].irq, kbase_tag(kbdev, i));
	}
}

void kbase_synchronize_irqs(struct kbase_device *kbdev)
{
	u32 nr = ARRAY_SIZE(kbase_handler_table);
	u32 i;

	for (i = 0; i < nr; i++) {
		if (kbdev->irqs[i].irq)
			synchronize_irq(kbdev->irqs[i].irq);
	}
}

#endif /* !defined(CONFIG_MALI_NO_MALI) */
