/*
 * arch/arm/mach-tegra/nvrm_user.c
 *
 * User-land access to NvRm APIs
 *
 * Copyright (c) 2008-2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/cpumask.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/platform_device.h>
#include <linux/freezer.h>
#include <linux/suspend.h>
#include <linux/percpu.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/smp.h>
#include <asm/smp_twd.h>
#include <asm/cpu.h>
#include "nvcommon.h"
#include "nvassert.h"
#include "nvos.h"
#include "nvrm_memmgr.h"
#include "nvrm_ioctls.h"
#include "mach/nvrm_linux.h"
#include "linux/nvos_ioctl.h"
#include "nvrm_power_private.h"
#include "nvreftrack.h"
#include "mach/timex.h"

pid_t s_nvrm_daemon_pid = 0;

NvError NvRm_Dispatch(void *InBuffer,
                      NvU32 InSize,
                      void *OutBuffer,
                      NvU32 OutSize,
                      NvDispatchCtx* Ctx);

static int nvrm_open(struct inode *inode, struct file *file);
static int nvrm_close(struct inode *inode, struct file *file);
static long nvrm_unlocked_ioctl(struct file *file,
    unsigned int cmd, unsigned long arg);
static int nvrm_mmap(struct file *file, struct vm_area_struct *vma);
extern void reset_cpu(unsigned int cpu, unsigned int reset);

static NvOsThreadHandle s_DfsThread = NULL;
static NvRtHandle s_RtHandle = NULL;

#define DEVICE_NAME "nvrm"

static const struct file_operations nvrm_fops =
{
    .owner = THIS_MODULE,
    .open = nvrm_open,
    .release = nvrm_close,
    .unlocked_ioctl = nvrm_unlocked_ioctl,
    .mmap = nvrm_mmap
};

static struct miscdevice nvrm_dev =
{
    .name = DEVICE_NAME,
    .fops = &nvrm_fops,
    .minor = MISC_DYNAMIC_MINOR,
};

#ifdef GHACK_DFS
static void NvRmDfsThread(void *args)
{
    NvRmDeviceHandle hRm = (NvRmDeviceHandle)args;
    struct cpumask cpu_mask;

    //Ensure that only cpu0 is in the affinity mask
    cpumask_clear(&cpu_mask);
    cpumask_set_cpu(0, &cpu_mask);
    if (sched_setaffinity(0, &cpu_mask))
    {
        panic("Unable to setaffinity of DFS thread!\n");
    }

    //Confirm that only CPU0 can run this thread
    if (!cpumask_test_cpu(0, &cpu_mask) || cpumask_weight(&cpu_mask) != 1)
    {
        panic("Unable to setaffinity of DFS thread!\n");
    }

    set_freezable_with_signal();

    if (NvRmDfsGetState(hRm) > NvRmDfsRunState_Disabled)
    {
        NvRmFreqKHz CpuKHz, f;
        CpuKHz = NvRmPrivDfsGetCurrentKHz(NvRmDfsClockId_Cpu);
        local_timer_rescale(CpuKHz);

        NvRmDfsSetState(hRm, NvRmDfsRunState_ClosedLoop);

        for (;;)
        {
            NvRmPmRequest Request = NvRmPrivPmThread();
            f = NvRmPrivDfsGetCurrentKHz(NvRmDfsClockId_Cpu);
            if (CpuKHz != f)
            {
                CpuKHz = f;
                local_timer_rescale(CpuKHz);
                twd_set_prescaler(NULL);
                smp_call_function(twd_set_prescaler, NULL, NV_TRUE);
            }
            if (Request & NvRmPmRequest_ExitFlag)
            {
                break;
            }
            if (Request & NvRmPmRequest_CpuOnFlag)
            {
#ifdef CONFIG_HOTPLUG_CPU
                printk("DFS requested CPU1 ON\n");
                preset_lpj = per_cpu(cpu_data, 0).loops_per_jiffy;
                cpu_up(1);
                smp_call_function(twd_set_prescaler, NULL, NV_TRUE);
#endif
            }

            if (Request & NvRmPmRequest_CpuOffFlag)
            {
#ifdef CONFIG_HOTPLUG_CPU
                printk("DFS requested CPU1 OFF\n");
                cpu_down(1);
#endif
            }
        }
    }
}
#endif

static void client_detach(NvRtClientHandle client)
{
    if (NvRtUnregisterClient(s_RtHandle, client))
    {
        NvDispatchCtx dctx;

        dctx.Rt = s_RtHandle;
        dctx.Client = client;
        dctx.PackageIdx = 0;

        for (;;)
        {
            void* ptr = NvRtFreeObjRef(&dctx,
                                       NvRtObjType_NvRm_NvRmMemHandle,
                                       NULL);
            WARN_ON_ONCE(ptr);
	    if (!ptr)
		    break;
            NVRT_LEAK("NvRm", "NvRmMemHandle", ptr);
        }

        NvRtUnregisterClient(s_RtHandle, client);
    }
}

int nvrm_open(struct inode *inode, struct file *file)
{
    NvRtClientHandle Client;

    if (NvRtRegisterClient(s_RtHandle, &Client) != NvSuccess)
    {
        return -ENOMEM;
    }

    file->private_data = (void*)Client;

    return 0;
}

int nvrm_close(struct inode *inode, struct file *file)
{
    client_detach((NvRtClientHandle)file->private_data);
    return 0;
}

long nvrm_unlocked_ioctl(struct file *file,
    unsigned int cmd, unsigned long arg)
{
    NvError err;
    NvOsIoctlParams p;
    NvU32 size;
    NvU32 small_buf[8];
    void *ptr = 0;
    long e;
    NvBool bAlloc = NV_FALSE;

    switch( cmd ) {
    case NvRmIoctls_Generic:
    {
        NvDispatchCtx dctx;

        dctx.Rt         = s_RtHandle;
        dctx.Client     = (NvRtClientHandle)file->private_data;
        dctx.PackageIdx = 0;

        err = NvOsCopyIn( &p, (void *)arg, sizeof(p) );
        if( err != NvSuccess )
        {
            printk( "NvRmIoctls_Generic: copy in failed\n" );
            goto fail;
        }

        //printk( "NvRmIoctls_Generic: %d %d %d\n", p.InBufferSize,
        //    p.InOutBufferSize, p.OutBufferSize );

        size = p.InBufferSize + p.InOutBufferSize + p.OutBufferSize;
        if( size <= sizeof(small_buf) )
        {
            ptr = small_buf;
        }
        else
        {
            ptr = NvOsAlloc( size );
            if( !ptr )
            {
                printk( "NvRmIoctls_Generic: alloc failure (%d bytes)\n",
                    size );
                goto fail;
            }

            bAlloc = NV_TRUE;
        }

        err = NvOsCopyIn( ptr, p.pBuffer, p.InBufferSize +
            p.InOutBufferSize );
        if( err != NvSuccess )
        {
            printk( "NvRmIoctls_Generic: copy in failure\n" );
            goto fail;
        }

        err = NvRm_Dispatch( ptr, p.InBufferSize + p.InOutBufferSize,
            ((NvU8 *)ptr) + p.InBufferSize, p.InOutBufferSize +
            p.OutBufferSize, &dctx );
        if( err != NvSuccess )
        {
            printk( "NvRmIoctls_Generic: dispatch failure\n" );
            goto fail;
        }

        if( p.InOutBufferSize || p.OutBufferSize )
        {
            err = NvOsCopyOut( ((NvU8 *)((NvOsIoctlParams *)arg)->pBuffer)
                + p.InBufferSize,
                ((NvU8 *)ptr) + p.InBufferSize,
                p.InOutBufferSize + p.OutBufferSize );
            if( err != NvSuccess )
            {
                printk( "NvRmIoctls_Generic: copy out failure\n" );
                goto fail;
            }
        }

        break;
    }
    case NvRmIoctls_NvRmGraphics:
        printk( "NvRmIoctls_NvRmGraphics: not supported\n" );
        goto fail;
    case NvRmIoctls_NvRmFbControl:
        printk( "NvRmIoctls_NvRmFbControl: deprecated \n" );
	break;

    case NvRmIoctls_NvRmMemRead:
    case NvRmIoctls_NvRmMemWrite:
    case NvRmIoctls_NvRmMemReadStrided:
    case NvRmIoctls_NvRmGetCarveoutInfo:
    case NvRmIoctls_NvRmMemWriteStrided:
        goto fail;

    case NvRmIoctls_NvRmMemMapIntoCallerPtr:
        // FIXME: implement?
        printk( "NvRmIoctls_NvRmMemMapIntoCallerPtr: not supported\n" );
        goto fail;
    case NvRmIoctls_NvRmBootDone:
#ifdef GHACK_DFS
        if (!s_DfsThread)
        {
            if (NvOsInterruptPriorityThreadCreate(NvRmDfsThread,
                    (void*)s_hRmGlobal, &s_DfsThread)!=NvSuccess)
            {
                NvOsDebugPrintf("Failed to create DFS processing thread\n");
                goto fail;
            }
        }
#endif
        break;
    case NvRmIoctls_NvRmGetClientId:
		err = NvOsCopyIn(&p, (void*)arg, sizeof(p));
		if (err != NvSuccess)
		{
			NvOsDebugPrintf("NvRmIoctls_NvRmGetClientId: copy in failed\n");
			goto fail;
		}

		NV_ASSERT(p.InBufferSize == 0);
		NV_ASSERT(p.OutBufferSize == sizeof(NvRtClientHandle));
		NV_ASSERT(p.InOutBufferSize == 0);

		if (NvOsCopyOut(p.pBuffer,
						&file->private_data,
						sizeof(NvRtClientHandle)) != NvSuccess)
		{
			NvOsDebugPrintf("Failed to copy client id\n");
			goto fail;
		}
		break;
	case NvRmIoctls_NvRmClientAttach:
	{
		NvRtClientHandle Client;

		err = NvOsCopyIn(&p, (void*)arg, sizeof(p));
		if (err != NvSuccess)
		{
			NvOsDebugPrintf("NvRmIoctls_NvRmClientAttach: copy in failed\n");
			goto fail;
		}

		NV_ASSERT(p.InBufferSize == sizeof(NvRtClientHandle));
		NV_ASSERT(p.OutBufferSize == 0);
		NV_ASSERT(p.InOutBufferSize == 0);

		if (NvOsCopyIn((void*)&Client,
					   p.pBuffer,
					   sizeof(NvRtClientHandle)) != NvSuccess)
		{
			NvOsDebugPrintf("Failed to copy client id\n");
			goto fail;
		}

		NV_ASSERT(Client || !"Bad client");

		if (Client == (NvRtClientHandle)file->private_data)
		{
			// The daemon is attaching to itself, no need to add refcount
			break;
		}
		if (NvRtAddClientRef(s_RtHandle, Client) != NvSuccess)
		{
			NvOsDebugPrintf("Client ref add unsuccessful\n");
			goto fail;
		}
		break;
	}
	case NvRmIoctls_NvRmClientDetach:
	{
		NvRtClientHandle Client;

		err = NvOsCopyIn(&p, (void*)arg, sizeof(p));
		if (err != NvSuccess)
		{
			NvOsDebugPrintf("NvRmIoctls_NvRmClientAttach: copy in failed\n");
			goto fail;
		}

		NV_ASSERT(p.InBufferSize == sizeof(NvRtClientHandle));
		NV_ASSERT(p.OutBufferSize == 0);
		NV_ASSERT(p.InOutBufferSize == 0);

		if (NvOsCopyIn((void*)&Client,
					   p.pBuffer,
					   sizeof(NvRtClientHandle)) != NvSuccess)
		{
			NvOsDebugPrintf("Failed to copy client id\n");
			goto fail;
		}

		NV_ASSERT(Client || !"Bad client");

		if (Client == (NvRtClientHandle)file->private_data)
		{
			// The daemon is detaching from itself, no need to dec refcount
			break;
		}

		client_detach(Client);
		break;
	}
	// FIXME: power ioctls?
	default:
		printk( "unknown ioctl code\n" );
		goto fail;
	}

	e = 0;
	goto clean;

fail:
	e = -EINVAL;

clean:
	if( bAlloc )
	{
		NvOsFree( ptr );
	}

	return e;
}

int nvrm_mmap(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

static int nvrm_probe(struct platform_device *pdev)
{
	int e = 0;
	NvU32 NumTypes = NvRtObjType_NvRm_Num;

	printk("nvrm probe\n");

	NV_ASSERT(s_RtHandle == NULL);

	if (NvRtCreate(1, &NumTypes, &s_RtHandle) != NvSuccess)
	{
		e = -ENOMEM;
	}

	if (e == 0)
	{
		e = misc_register( &nvrm_dev );
	}

	if( e < 0 )
	{
		if (s_RtHandle)
		{
			NvRtDestroy(s_RtHandle);
			s_RtHandle = NULL;
		}

		printk("nvrm probe failed to open\n");
	}
	return e;
}

static int nvrm_remove(struct platform_device *pdev)
{
	misc_deregister( &nvrm_dev );
	NvRtDestroy(s_RtHandle);
	s_RtHandle = NULL;
	return 0;
}

static int nvrm_suspend(struct platform_device *pdev, pm_message_t state)
{
#ifdef GHACK
	if(NvRmKernelPowerSuspend(s_hRmGlobal)) {
		printk(KERN_INFO "%s : FAILED\n", __func__);
		return -1;
	}
#endif
	return 0;
}

static int nvrm_resume(struct platform_device *pdev)
{
#ifdef GHACK
	if(NvRmKernelPowerResume(s_hRmGlobal)) {
		printk(KERN_INFO "%s : FAILED\n", __func__);
		return -1;
	}
#endif
	return 0;

}

static struct platform_driver nvrm_driver =
{
	.probe	 = nvrm_probe,
	.remove	 = nvrm_remove,
	.suspend = nvrm_suspend,
	.resume	 = nvrm_resume,
	.driver	 = { .name = "nvrm" }
};

#if defined(CONFIG_PM)
//
// /sys/power/nvrm/notifier
//

wait_queue_head_t tegra_pm_notifier_wait;
wait_queue_head_t sys_nvrm_notifier_wait;

int tegra_pm_notifier_continue_ok;

struct kobject *nvrm_kobj;

const char* sys_nvrm_notifier;

static const char *STRING_PM_SUSPEND_PREPARE = "PM_SUSPEND_PREPARE";
static const char *STRING_PM_POST_SUSPEND    = "PM_POST_SUSPEND";
static const char *STRING_PM_DISPLAY_OFF     = "PM_DISPLAY_OFF";
static const char *STRING_PM_DISPLAY_ON      = "PM_DISPLAY_ON";
static const char *STRING_PM_CONTINUE        = "PM_CONTINUE";
static const char *STRING_PM_SIGNAL          = "PM_SIGNAL";

// Reading blocks if the value is not available.
static ssize_t
nvrm_notifier_show(struct kobject *kobj, struct kobj_attribute *attr,
		   char *buf)
{
	int nchar;

	// Block if the value is not available yet.
	if (! sys_nvrm_notifier)
	{
	    printk(KERN_INFO "%s: blocking\n", __func__);
	    wait_event_interruptible(sys_nvrm_notifier_wait, sys_nvrm_notifier);
	}

	// In case of false wakeup, return "".
	if (! sys_nvrm_notifier)
	{
	    printk(KERN_INFO "%s: false wakeup, returning with '\\n'\n", __func__);
	    nchar = sprintf(buf, "\n");
	    return nchar;
	}

	// Return the value, and clear.
	printk(KERN_INFO "%s: returning with '%s'\n", __func__, sys_nvrm_notifier);
	nchar = sprintf(buf, "%s\n", sys_nvrm_notifier);
	sys_nvrm_notifier = NULL;
	return nchar;
}

// Writing is no blocking.
static ssize_t
nvrm_notifier_store(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t count)
{
	if (!strncmp(buf, STRING_PM_CONTINUE, strlen(STRING_PM_CONTINUE))) {
		// Wake up pm_notifier.
		tegra_pm_notifier_continue_ok = 1;
		wake_up(&tegra_pm_notifier_wait);
	}
	else if (!strncmp(buf, STRING_PM_SIGNAL, strlen(STRING_PM_SIGNAL))) {
		s_nvrm_daemon_pid = 0;
		sscanf(buf, "%*s %d", &s_nvrm_daemon_pid);
		printk(KERN_INFO "%s: nvrm_daemon=%d\n", __func__, s_nvrm_daemon_pid);
	}
	else {
		printk(KERN_ERR "%s: wrong value '%s'\n", __func__, buf);
	}

	return count;
}

static struct kobj_attribute nvrm_notifier_attribute =
	__ATTR(notifier, 0666, nvrm_notifier_show, nvrm_notifier_store);

//
// PM notifier
//

static void notify_daemon(const char* notice)
{
	long timeout = HZ * 30;

	// In case daemon's pid is not reported, do not signal or wait.
	if (!s_nvrm_daemon_pid) {
		printk(KERN_ERR "%s: don't know nvrm_daemon's PID\n", __func__);
		return;
	}

	// Clear before kicking nvrm_daemon.
	tegra_pm_notifier_continue_ok = 0;

	// Notify nvrm_daemon.
	sys_nvrm_notifier = notice;
	wake_up(&sys_nvrm_notifier_wait);

	// Wait for the reply from nvrm_daemon.
	printk(KERN_INFO "%s: wait for nvrm_daemon\n", __func__);
	if (wait_event_timeout(tegra_pm_notifier_wait,
			       tegra_pm_notifier_continue_ok, timeout) == 0) {
	    printk(KERN_ERR "%s: timed out. nvrm_daemon did not reply\n", __func__);
	}

	// Go back to the initial state.
	sys_nvrm_notifier = NULL;
}

int tegra_pm_notifier(struct notifier_block *nb,
			  unsigned long event, void *nouse)
{
	printk(KERN_INFO "%s: start processing event=%lx\n", __func__, event);

	// Notify the event to nvrm_daemon.
	if (event == PM_SUSPEND_PREPARE) {
#ifndef CONFIG_HAS_EARLYSUSPEND
		notify_daemon(STRING_PM_DISPLAY_OFF);
#endif
		notify_daemon(STRING_PM_SUSPEND_PREPARE);
	}
	else if (event == PM_POST_SUSPEND) {
		notify_daemon(STRING_PM_POST_SUSPEND);
#ifndef CONFIG_HAS_EARLYSUSPEND
		notify_daemon(STRING_PM_DISPLAY_ON);
#endif
	}
	else {
		printk(KERN_ERR "%s: unknown event %ld\n", __func__, event);
		return NOTIFY_DONE;
	}

	printk(KERN_INFO "%s: finished processing event=%ld\n", __func__, event);
	return NOTIFY_OK;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
void tegra_display_off(struct early_suspend *h)
{
	notify_daemon(STRING_PM_DISPLAY_OFF);
}

void tegra_display_on(struct early_suspend *h)
{
	notify_daemon(STRING_PM_DISPLAY_ON);
}

static struct early_suspend tegra_display_power =
{
	.suspend = tegra_display_off,
	.resume = tegra_display_on,
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB
};
#endif
#endif

static struct platform_device nvrm_device =
{
    .name = "nvrm"
};


static int __init nvrm_init(void)
{
	int ret = 0;
	printk(KERN_INFO "%s called\n", __func__);

	#if defined(CONFIG_PM)
	// Register PM notifier.
	pm_notifier(tegra_pm_notifier, 0);
	tegra_pm_notifier_continue_ok = 0;
	init_waitqueue_head(&tegra_pm_notifier_wait);

	#if defined(CONFIG_HAS_EARLYSUSPEND)
	register_early_suspend(&tegra_display_power);
	#endif

	// Create /sys/power/nvrm/notifier.
	nvrm_kobj = kobject_create_and_add("nvrm", power_kobj);
	sysfs_create_file(nvrm_kobj, &nvrm_notifier_attribute.attr);
	sys_nvrm_notifier = NULL;
	init_waitqueue_head(&sys_nvrm_notifier_wait);
	#endif

	// Register NvRm platform driver.
	ret = platform_driver_register(&nvrm_driver);

	platform_device_register(&nvrm_device);

	return ret;
}

static void __exit nvrm_deinit(void)
{
    printk(KERN_INFO "%s called\n", __func__);
    platform_driver_unregister(&nvrm_driver);
}

module_init(nvrm_init);
module_exit(nvrm_deinit);
