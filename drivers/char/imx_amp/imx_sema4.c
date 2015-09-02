/*
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/imx_sema4.h>

static struct imx_sema4_mutex_device *imx6_sema4;

/*!
 * \brief mutex create function.
 *
 * This function allocates imx_sema4_mutex structure and returns a handle
 * to it. The mutex to be created is identified by SEMA4 device number and mutex
 * (gate) number. The handle is used to reference the created mutex in calls to
 * other imx_sema4_mutex API functions. This function is to be called only
 * once for each mutex.
 *
 * \param[in] dev_num     SEMA4 device (module) number.
 * \param[in] mutex_num   Mutex (gate) number.
 *
 * \return NULL (Failure.)
 * \return imx_sema4_mutex (Success.)
 */
struct imx_sema4_mutex *
imx_sema4_mutex_create(u32 dev_num, u32 mutex_num)
{
	struct imx_sema4_mutex *mutex_ptr = NULL;

	if ((mutex_num > SEMA4_NUM_GATES) || dev_num >= SEMA4_NUM_DEVICES)
		goto out;

	if (imx6_sema4->cpine_val & (1 < mutex_num)) {
		pr_err("Error: requiring a allocated sema4.\n");
		pr_err("mutex_num %d cpine_val 0x%08x.\n",
				mutex_num, imx6_sema4->cpine_val);
	}
	mutex_ptr = kzalloc(sizeof(*mutex_ptr), GFP_KERNEL);
	if (!mutex_ptr)
		goto out;
	imx6_sema4->mutex_ptr[mutex_num] = mutex_ptr;
	imx6_sema4->alloced |= 1 < mutex_num;
	imx6_sema4->cpine_val |= idx_sema4[mutex_num];
	writew(imx6_sema4->cpine_val, imx6_sema4->ioaddr + SEMA4_CP0INE);

	mutex_ptr->valid = CORE_MUTEX_VALID;
	mutex_ptr->gate_num = mutex_num;
	init_waitqueue_head(&mutex_ptr->wait_q);

out:
	return mutex_ptr;
}
EXPORT_SYMBOL(imx_sema4_mutex_create);

/*!
 * \brief mutex destroy function.
 *
 * This function destroys a mutex.
 *
 * \param[in] mutex_ptr   Pointer to mutex structure.
 *
 * \return MQX_COMPONENT_DOES_NOT_EXIST (mutex component not installed.)
 * \return MQX_INVALID_PARAMETER (Wrong input parameter.)
 * \return COREMUTEX_OK (Success.)
 *
 */
int imx_sema4_mutex_destroy(struct imx_sema4_mutex *mutex_ptr)
{
	u32 mutex_num;

	if ((mutex_ptr == NULL) || (mutex_ptr->valid != CORE_MUTEX_VALID))
		return -EINVAL;

	mutex_num = mutex_ptr->gate_num;
	if ((imx6_sema4->cpine_val & idx_sema4[mutex_num]) == 0) {
		pr_err("Error: trying to destroy a un-allocated sema4.\n");
		pr_err("mutex_num %d cpine_val 0x%08x.\n",
				mutex_num, imx6_sema4->cpine_val);
	}
	imx6_sema4->mutex_ptr[mutex_num] = NULL;
	imx6_sema4->alloced &= ~(1 << mutex_num);
	imx6_sema4->cpine_val &= ~(idx_sema4[mutex_num]);
	writew(imx6_sema4->cpine_val, imx6_sema4->ioaddr + SEMA4_CP0INE);

	kfree(mutex_ptr);

	return 0;
}
EXPORT_SYMBOL(imx_sema4_mutex_destroy);

/*!
 * \brief Lock the mutex, shouldn't be interruted by INT.
 *
 * This function attempts to lock a mutex. If the mutex is already locked
 * by another task the function return -EBUSY, and tell invoker wait until
 * it is possible to lock the mutex.
 *
 * \param[in] mutex_ptr   Pointer to mutex structure.
 *
 * \return MQX_INVALID_POINTER (Wrong pointer to the mutex structure provided.)
 * \return COREMUTEX_OK (mutex successfully locked.)
 *
 * \see imx_sema4_mutex_unlock
 */
int _imx_sema4_mutex_lock(struct imx_sema4_mutex *mutex_ptr)
{
	int ret = 0, i = mutex_ptr->gate_num;

	if ((mutex_ptr == NULL) || (mutex_ptr->valid != CORE_MUTEX_VALID))
		return -EINVAL;

	mutex_ptr->gate_val = readb(imx6_sema4->ioaddr + i);
	mutex_ptr->gate_val &= SEMA4_GATE_MASK;
	/* Check to see if this core already own it */
	if (mutex_ptr->gate_val == SEMA4_A9_LOCK) {
		/* return -EBUSY, invoker should be in sleep, and re-lock ag */
		pr_err("%s -> %s %d already locked, wait! num %d val %d.\n",
				__FILE__, __func__, __LINE__,
				i, mutex_ptr->gate_val);
		ret = -EBUSY;
		goto out;
	} else {
		/* try to lock the mutex */
		mutex_ptr->gate_val = readb(imx6_sema4->ioaddr + i);
		mutex_ptr->gate_val &= (~SEMA4_GATE_MASK);
		mutex_ptr->gate_val |= SEMA4_A9_LOCK;
		writeb(mutex_ptr->gate_val, imx6_sema4->ioaddr + i);
		mutex_ptr->gate_val = readb(imx6_sema4->ioaddr + i);
		mutex_ptr->gate_val &= SEMA4_GATE_MASK;
		/* double check the mutex is locked, otherwise, return -EBUSY */
		if (mutex_ptr->gate_val != SEMA4_A9_LOCK) {
			pr_debug("wait-locked num %d val %d.\n",
					i, mutex_ptr->gate_val);
			ret = -EBUSY;
		}
	}
out:
	return ret;
}

/* !
 * \brief Try to lock the core mutex.
 *
 * This function attempts to lock a mutex. If the mutex is successfully locked
 * for the calling task, SEMA4_A9_LOCK is returned. If the mutex is already
 * locked by another task, the function does not block but rather returns
 * negative immediately.
 *
 * \param[in] mutex_ptr  Pointer to core_mutex structure.
 *
 * \return SEMA4_A9_LOCK (mutex successfully locked.)
 * \return negative (mutex not locked.)
 *
 */
int imx_sema4_mutex_trylock(struct imx_sema4_mutex *mutex_ptr)
{
	int ret = 0;

	ret = _imx_sema4_mutex_lock(mutex_ptr);
	if (ret == 0)
		return SEMA4_A9_LOCK;
	else
		return ret;
}
EXPORT_SYMBOL(imx_sema4_mutex_trylock);

/*!
 * \brief Invoke _imx_sema4_mutex_lock to lock the mutex.
 *
 * This function attempts to lock a mutex. If the mutex is already locked
 * by another task the function, sleep itself and schedule out.
 * Wait until it is possible to lock the mutex.
 *
 * Invoker should add its own wait queue into the wait queue header of the
 * required semaphore, set TASK_INTERRUPTIBLE and sleep on itself by
 * schedule() when the lock is failed. Re-try to lock the semaphore when
 * it is woke up by the sema4 isr.
 *
 * \param[in] mutex_ptr  Pointer to mutex structure.
 *
 * \return SEMA4_A9_LOCK (mutex successfully locked.)
 *
 * \see imx_sema4_mutex_unlock
 */
int imx_sema4_mutex_lock(struct imx_sema4_mutex *mutex_ptr)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&imx6_sema4->lock, flags);
	ret = _imx_sema4_mutex_lock(mutex_ptr);
	spin_unlock_irqrestore(&imx6_sema4->lock, flags);
	while (-EBUSY == ret) {
		spin_lock_irqsave(&imx6_sema4->lock, flags);
		ret = _imx_sema4_mutex_lock(mutex_ptr);
		spin_unlock_irqrestore(&imx6_sema4->lock, flags);
		if (ret == 0)
			break;
	}

	return ret;
}
EXPORT_SYMBOL(imx_sema4_mutex_lock);

/*!
 * \brief Unlock the mutex.
 *
 * This function unlocks the specified mutex.
 *
 * \param[in] mutex_ptr   Pointer to mutex structure.
 *
 * \return -EINVAL (Wrong pointer to the mutex structure provided.)
 * \return -EINVAL (This mutex has not been locked by this core.)
 * \return 0 (mutex successfully unlocked.)
 *
 * \see imx_sema4_mutex_lock
 */
int imx_sema4_mutex_unlock(struct imx_sema4_mutex *mutex_ptr)
{
	int ret = 0, i = mutex_ptr->gate_num;

	if ((mutex_ptr == NULL) || (mutex_ptr->valid != CORE_MUTEX_VALID))
		return -EINVAL;

	mutex_ptr->gate_val = readb(imx6_sema4->ioaddr + i);
	mutex_ptr->gate_val &= SEMA4_GATE_MASK;
	/* make sure it is locked by this core */
	if (mutex_ptr->gate_val != SEMA4_A9_LOCK) {
		pr_err("%d Trying to unlock an unlock mutex.\n", __LINE__);
		ret = -EINVAL;
		goto out;
	}
	/* unlock it */
	mutex_ptr->gate_val = readb(imx6_sema4->ioaddr + i);
	mutex_ptr->gate_val &= (~SEMA4_GATE_MASK);
	writeb(mutex_ptr->gate_val | SEMA4_UNLOCK, imx6_sema4->ioaddr + i);
	mutex_ptr->gate_val = readb(imx6_sema4->ioaddr + i);
	mutex_ptr->gate_val &= SEMA4_GATE_MASK;
	/* make sure it is locked by this core */
	if (mutex_ptr->gate_val == SEMA4_A9_LOCK)
		pr_err("%d ERROR, failed to unlock the mutex.\n", __LINE__);

out:
	return ret;
}
EXPORT_SYMBOL(imx_sema4_mutex_unlock);

/*
 * isr used by SEMA4, wake up the sleep tasks if there are the tasks waiting
 * for locking semaphore.
 * FIXME the bits order of the gatn, cpnie, cpnntf are not exact identified yet!
 */
static irqreturn_t imx_sema4_isr(int irq, void *dev_id)
{
	int i;
	struct imx_sema4_mutex *mutex_ptr;
	u32 mask;
	struct imx_sema4_mutex_device *imx6_sema4 = dev_id;

	imx6_sema4->cpntf_val = readw(imx6_sema4->ioaddr + SEMA4_CP0NTF);
	for (i = 0; i < SEMA4_NUM_GATES; i++) {
		mask = idx_sema4[i];
		if ((imx6_sema4->cpntf_val) & mask) {
			mutex_ptr = imx6_sema4->mutex_ptr[i];
			/*
			 * An interrupt is pending on this mutex, the only way
			 * to clear it is to lock it (either by this core or
			 * another).
			 */
			mutex_ptr->gate_val = readb(imx6_sema4->ioaddr + i);
			mutex_ptr->gate_val &= (~SEMA4_GATE_MASK);
			mutex_ptr->gate_val |= SEMA4_A9_LOCK;
			writeb(mutex_ptr->gate_val, imx6_sema4->ioaddr + i);
			mutex_ptr->gate_val = readb(imx6_sema4->ioaddr + i);
			mutex_ptr->gate_val &= SEMA4_GATE_MASK;
			if (mutex_ptr->gate_val == SEMA4_A9_LOCK) {
				/*
				 * wake up the wait queue, whatever there
				 * are wait task or not.
				 * NOTE: check gate is locted or not in
				 * sema4_lock func by wait task.
				 */
				mutex_ptr->gate_val =
					readb(imx6_sema4->ioaddr + i);
				mutex_ptr->gate_val &= (~SEMA4_GATE_MASK);
				mutex_ptr->gate_val |= SEMA4_UNLOCK;

				writeb(mutex_ptr->gate_val,
						imx6_sema4->ioaddr + i);
				wake_up(&mutex_ptr->wait_q);
			} else {
				pr_debug("can't lock gate%d %s retry!\n", i,
						mutex_ptr->gate_val ?
						"locked by m4" : "");
			}
		}
	}

	return IRQ_HANDLED;
}

static const struct of_device_id imx_sema4_dt_ids[] = {
	{ .compatible = "fsl,imx6sx-sema4", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_sema4_dt_ids);

static int imx_sema4_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret;

	imx6_sema4 = devm_kzalloc(&pdev->dev, sizeof(*imx6_sema4), GFP_KERNEL);
	if (!imx6_sema4)
		return -ENOMEM;

	imx6_sema4->dev = &pdev->dev;
	imx6_sema4->cpine_val = 0;
	spin_lock_init(&imx6_sema4->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR(res)) {
		dev_err(&pdev->dev, "unable to get imx sema4 resource 0\n");
		ret = -ENODEV;
		goto err;
	}

	imx6_sema4->ioaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(imx6_sema4->ioaddr)) {
		ret = PTR_ERR(imx6_sema4->ioaddr);
		goto err;
	}

	imx6_sema4->irq = platform_get_irq(pdev, 0);
	if (!imx6_sema4->irq) {
		dev_err(&pdev->dev, "failed to get irq\n");
		ret = -ENODEV;
		goto err;
	}

	ret = devm_request_irq(&pdev->dev, imx6_sema4->irq, imx_sema4_isr,
				IRQF_SHARED, "imx6sx-sema4", imx6_sema4);
	if (ret) {
		dev_err(&pdev->dev, "failed to request imx sema4 irq\n");
		ret = -ENODEV;
		goto err;
	}

	platform_set_drvdata(pdev, imx6_sema4);

err:
	return ret;
}

static int imx_sema4_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver imx_sema4_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "imx-sema4",
		   .of_match_table = imx_sema4_dt_ids,
		   },
	.probe = imx_sema4_probe,
	.remove = imx_sema4_remove,
};

static int __init imx_sema4_init(void)
{
	int ret;

	ret = platform_driver_register(&imx_sema4_driver);
	if (ret)
		pr_err("Unable to initialize sema4 driver\n");
	else
		pr_info("imx sema4 driver is registered.\n");

	return ret;
}

static void __exit imx_sema4_exit(void)
{
	pr_info("imx sema4 driver is unregistered.\n");
	platform_driver_unregister(&imx_sema4_driver);
}

module_exit(imx_sema4_exit);
module_init(imx_sema4_init);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("IMX SEMA4 driver");
MODULE_LICENSE("GPL");
