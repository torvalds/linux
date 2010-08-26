/*
 * arch/arm/mach-tegra/nvrpc_user.c
 *
 * User-land access to NvRm transport APIs
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

#define NV_DEBUG 0

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <mach/nvrm_linux.h>
#include <mach/nvrpc.h>
#include "nvcommon.h"
#include "nvassert.h"
#include "nvos.h"
#include "nvrm_transport.h"
#include "nvrm_xpc.h"

#define DEVICE_NAME "nvrpc"
#define NVRPC_MAX_LOCAL_STACK   256
#define nvrpc_stack_kzalloc(stackbuf, size, gfp) \
            ((size) > sizeof((stackbuf)) ? kzalloc((size),(gfp)) : (stackbuf))
#define nvrpc_stack_kfree(stackbuf, buf) \
            do { \
                if ((buf) && (buf)!=(void *)(stackbuf)) \
                kfree(buf); \
            } while (0);

static int nvrpc_open(struct inode *inode, struct file *file);
static int nvrpc_close(struct inode *inode, struct file *file);
static long nvrpc_unlocked_ioctl(struct file *file,
    unsigned int cmd, unsigned long arg);

//Ioctl functions
static int nvrpc_ioctl_open(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_get_port_name(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_close(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_wait_for_connect(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_connect(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_set_queue_depth(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_send_msg(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_send_msg_lp0(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_recv_msg(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_xpc_init(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_xpc_acquire(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_xpc_release(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_xpc_get_msg(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_xpc_send_msg(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_xpc_destroy(struct file *filp,
	unsigned int cmd, void __user *arg);
static int nvrpc_ioctl_xpc_create(struct file *filp,
	unsigned int cmd, void __user *arg);
// local function
static int nvrpc_make_error_code(NvError e);

static const struct file_operations nvrpc_fops =
{
	.owner		= THIS_MODULE,
	.open		= nvrpc_open,
	.release	= nvrpc_close,
	.unlocked_ioctl	= nvrpc_unlocked_ioctl,
};

static struct miscdevice nvrpc_dev =
{
	.name	= DEVICE_NAME,
	.fops	= &nvrpc_fops,
	.minor	= MISC_DYNAMIC_MINOR,
};

static DEFINE_MUTEX(nvrpc_device_lock);

static NvBool s_init_done = NV_FALSE;
NvRmDeviceHandle s_hRmGlobal = NULL;

int nvrpc_open(struct inode *inode, struct file *file)
{
	NvError e = NvSuccess;

	mutex_lock(&nvrpc_device_lock);
	if (s_init_done == NV_FALSE) {
		e = NvRmTransportInit(s_hRmGlobal);
		s_init_done = NV_TRUE;
	}
	mutex_unlock(&nvrpc_device_lock);

	if (e == NvSuccess)
		return 0;
	else
		return -ENODEV;
}

int nvrpc_close(struct inode *inode, struct file *file)
{
	return 0;
}

static int nvrpc_make_error_code(NvError e)
{
	int error = 0;
	if (error != NvSuccess) {
		if (e == NvError_InvalidAddress)
			error = -EFAULT;
		else if (e == NvError_BadParameter)
			error = -EINVAL;
		else
			error = -EIO;
	}
	return error;
}

NvRmTransportHandle g_hTransportAvp = NULL;
NvRmTransportHandle g_hTransportCpu = NULL;
NvOsSemaphoreHandle g_hTransportAvpSem = NULL;
NvOsSemaphoreHandle g_hTransportCpuSem = NULL;
int g_hTransportAvpIsConnected = 0;
int g_hTransportCpuIsConnected = 0;

static int nvrpc_ioctl_open(struct file *filp,
			    unsigned int cmd, void __user *arg)
{
	NvError e = NvSuccess;
	int error;
	struct nvrpc_open_params op;
	char *p_name = NULL;
	NvOsSemaphoreHandle recv_sem = NULL;
	NvU32 port_name[NVRPC_MAX_LOCAL_STACK/sizeof(NvU32)];

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;

	if (op.port_name_size) {
		p_name = nvrpc_stack_kzalloc(port_name,
					     op.port_name_size, GFP_KERNEL);
		if (!p_name) {
			error = -ENOMEM;
			goto fail;
		}
		error = copy_from_user(p_name, (const void*)op.port_name,
				       op.port_name_size);
		if (error)
			goto fail;
		if (p_name[op.port_name_size - 1] != 0) {
			error = -EINVAL;
			goto fail;
		}
	}
	if (op.sem) {
		NvOsSemaphoreHandle sem = (NvOsSemaphoreHandle) op.sem;
		e = NvOsSemaphoreUnmarshal(sem, &recv_sem);
		if (e != NvSuccess)
			goto fail;
	}
	op.ret_val = NvRmTransportOpen(s_hRmGlobal, p_name, recv_sem,
				       (void *)&op.transport_handle);
	error = copy_to_user(arg, &op, sizeof(op));
	if (p_name && ! strcmp(p_name, "RPC_CPU_PORT")) {
	    if (g_hTransportCpu) {
		    panic("%s: g_hTransportCpu=%p is already assigned.\n", __func__, g_hTransportCpu);
	    }
	    g_hTransportCpu = (NvRmTransportHandle)op.transport_handle;
	    g_hTransportCpuSem = (NvOsSemaphoreHandle) op.sem;
	}
	if (p_name && ! strcmp(p_name, "RPC_AVP_PORT")) {
	    if (g_hTransportAvp) {
		    panic("%s: g_hTransportAvp=%p is already assigned.\n", __func__, g_hTransportAvp);
	    }
	    g_hTransportAvp = (NvRmTransportHandle)op.transport_handle;
	    g_hTransportAvpSem = (NvOsSemaphoreHandle) op.sem;
	}

fail:
	nvrpc_stack_kfree((char*)port_name, p_name);
	if (recv_sem)
		NvOsSemaphoreDestroy(recv_sem);
	if (e != NvSuccess)
		error = nvrpc_make_error_code(e);
	return error;
}

static int nvrpc_ioctl_get_port_name(struct file *filp,
				     unsigned int cmd, void __user *arg)
{
	int error;

	struct nvrpc_open_params op;
	NvS8 *p_name = NULL;
	NvU32 port_name[NVRPC_MAX_LOCAL_STACK/sizeof(NvU32)];

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	if (op.port_name_size && op.port_name) {
		p_name = nvrpc_stack_kzalloc(port_name,
					     op.port_name_size, GFP_KERNEL);
		if (!p_name) {
			error = -ENOMEM;
			goto fail;
		}
	}
	NvRmTransportGetPortName((NvRmTransportHandle)op.transport_handle,
				 p_name, op.port_name_size);

	if (op.port_name_size && p_name) {
		error = copy_to_user((void*)op.port_name,
				     p_name, op.port_name_size * sizeof(NvU8));
	}

fail:
	nvrpc_stack_kfree((NvS8*)port_name, p_name);
	return error;
}

static int nvrpc_ioctl_close(struct file *filp,
			     unsigned int cmd, void __user *arg)
{
	int error;
	struct nvrpc_handle_param op;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	NvRmTransportClose((void*)op.handle);

fail:
	return error;
}

static int nvrpc_ioctl_wait_for_connect(struct file *filp,
					unsigned int cmd, void __user *arg)
{
	NvError e = NvSuccess;
	int error;
	struct nvrpc_handle_param op;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	op.ret_val = NvRmTransportWaitForConnect(
		(void *)op.handle, op.param);
	error = copy_to_user(arg, &op, sizeof(op));

fail:
	if (e != NvSuccess)
		error = nvrpc_make_error_code(e);
	return error;
}

static int nvrpc_ioctl_connect(struct file *filp,
			       unsigned int cmd, void __user *arg)
{
	NvError e = NvSuccess;
	int error;
	struct nvrpc_handle_param op;
	NvU8 port_name[NVRPC_MAX_LOCAL_STACK];

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;

	NvRmTransportGetPortName((void *)op.handle,
				 port_name, sizeof(port_name));


	op.ret_val = NvRmTransportConnect(
		(void *)op.handle, op.param);
	error = copy_to_user(arg, &op, sizeof(op));

	if (! strcmp(port_name, "RPC_AVP_PORT")) {
	    g_hTransportAvpIsConnected = 1;
	}
	if (! strcmp(port_name, "RPC_CPU_PORT")) {
	    g_hTransportCpuIsConnected = 1;
	}

fail:
	if (e != NvSuccess)
		error = nvrpc_make_error_code(e);
	return error;
}

static int nvrpc_ioctl_set_queue_depth(struct file *filp,
				       unsigned int cmd, void __user *arg)
{
	NvError e = NvSuccess;
	int error;
	struct nvrpc_set_queue_depth_params op;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	op.ret_val = NvRmTransportSetQueueDepth(
		(NvRmTransportHandle)op.transport_handle,
		op.max_queue_depth,
		op.max_message_size);
	error = copy_to_user(arg, &op, sizeof(op));

fail:
	if (e != NvSuccess)
		error = nvrpc_make_error_code(e);
	return error;
}

static int nvrpc_ioctl_send_msg(struct file *filp,
				unsigned int cmd, void __user *arg)
{
	int error;
	struct nvrpc_msg_params op;
	void* msg_buffer = NULL;
	NvU32 buffer[NVRPC_MAX_LOCAL_STACK/sizeof(NvU32)];

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	if (op.msg_buffer && op.max_message_size) {
		msg_buffer = nvrpc_stack_kzalloc(buffer,
						 op.max_message_size,
						 GFP_KERNEL);
		if (!msg_buffer) {
			error = -ENOMEM;
			goto fail;
		}
		error = copy_from_user(msg_buffer,
				       (void*)op.msg_buffer,
				       op.max_message_size);
        if (error)
		goto fail;
	}

	op.ret_val = NvRmTransportSendMsg(
		(NvRmTransportHandle)op.transport_handle,
		msg_buffer, op.max_message_size, op.params);
	error = copy_to_user(arg, &op, sizeof(op));

fail:
	nvrpc_stack_kfree(buffer, msg_buffer);
	return error;
}

static int nvrpc_ioctl_send_msg_lp0(struct file *filp,
				    unsigned int cmd, void __user *arg)
{
	int error;
	struct nvrpc_msg_params op;
	void* msg_buffer = NULL;
	NvU32 buffer[NVRPC_MAX_LOCAL_STACK/sizeof(NvU32)];

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	if (op.msg_buffer && op.max_message_size)  {
        msg_buffer = nvrpc_stack_kzalloc(buffer,
					 op.max_message_size, GFP_KERNEL);
        if (!msg_buffer) {
		error = -ENOMEM;
		goto fail;
        }
        error = copy_from_user(msg_buffer, (void*)op.msg_buffer,
			       op.max_message_size);
        if (error)
		goto fail;
	}
	op.ret_val = NvRmTransportSendMsgInLP0(
		(NvRmTransportHandle)op.transport_handle,
		msg_buffer, op.max_message_size);
	error = copy_to_user(arg, &op, sizeof(op));

fail:
	nvrpc_stack_kfree(buffer, msg_buffer);
	return error;
}

static int nvrpc_ioctl_recv_msg(struct file *filp,
				unsigned int cmd, void __user *arg)
{
	int error;
	struct nvrpc_msg_params op;
	void* msg_buffer = NULL;
	NvU32 buffer[NVRPC_MAX_LOCAL_STACK/sizeof(NvU32)];

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	if (op.msg_buffer && op.max_message_size) {
		msg_buffer = nvrpc_stack_kzalloc(buffer,
			op.max_message_size, GFP_KERNEL);
		if (!msg_buffer) {
			error = -ENOMEM;
			goto fail;
		}
	} else {
		error = -EINVAL;
		goto fail;
	}
	op.ret_val = NvRmTransportRecvMsg(
		(NvRmTransportHandle)op.transport_handle,
		msg_buffer, op.max_message_size, &op.params);
	error = copy_to_user(arg, &op, sizeof(op));
	if (op.msg_buffer && msg_buffer) {
		error = copy_to_user((void*)op.msg_buffer,
                    msg_buffer, op.max_message_size);
		if (error)
			goto fail;
	}

fail:
	nvrpc_stack_kfree(buffer, msg_buffer);
	return error;
}

static int nvrpc_ioctl_xpc_init(struct file *filp,
				unsigned int cmd, void __user *arg)
{
	int error;
	struct nvrpc_handle_param op;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	op.ret_val = NvRmXpcInitArbSemaSystem((void *)op.handle);
	error = copy_to_user(arg, &op, sizeof(op));

fail:
	return error;
}

static int nvrpc_ioctl_xpc_acquire(struct file *filp,
				   unsigned int cmd, void __user *arg)
{
	int error;
	struct nvrpc_handle_param op;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	NvRmXpcModuleAcquire(op.param);

fail:
	return error;
}

static int nvrpc_ioctl_xpc_release(struct file *filp,
				   unsigned int cmd, void __user *arg)
{
	int error;
	struct nvrpc_handle_param op;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	NvRmXpcModuleRelease(op.param);

fail:
	return error;
}

static int nvrpc_ioctl_xpc_get_msg(struct file *filp,
				   unsigned int cmd, void __user *arg)
{
	int error;
	struct nvrpc_handle_param op;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	op.ret_val = NvRmPrivXpcGetMessage(
		(NvRmPrivXpcMessageHandle)op.handle);
	error = copy_to_user(arg, &op, sizeof(op));

fail:
	return error;
}

static int nvrpc_ioctl_xpc_send_msg(struct file *filp,
				    unsigned int cmd, void __user *arg)
{
	int error;
	struct nvrpc_handle_param op;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	op.ret_val = NvRmPrivXpcSendMessage(
		(NvRmPrivXpcMessageHandle)op.handle, op.param);
	error = copy_to_user(arg, &op, sizeof(op));

fail:
	return error;
}

static int nvrpc_ioctl_xpc_destroy(struct file *filp,
				   unsigned int cmd, void __user *arg)
{
	int error;
	struct nvrpc_handle_param op;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	NvRmPrivXpcDestroy((NvRmPrivXpcMessageHandle)op.handle);

fail:
	return error;
}

static int nvrpc_ioctl_xpc_create(struct file *filp,
				  unsigned int cmd, void __user *arg)
{
	int error;
	struct nvrpc_handle_param op;

	error = copy_from_user(&op, arg, sizeof(op));
	if (error)
		goto fail;
	op.ret_val = NvRmPrivXpcCreate((NvRmDeviceHandle)op.handle,
				       (void*)&op.param);
	error = copy_to_user(&op, arg, sizeof(op));

fail:
	return error;
}


static long nvrpc_unlocked_ioctl(struct file *file,
				 unsigned int cmd, unsigned long arg)
{
	int err = 0;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != NVRPC_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, uarg, _IOC_SIZE(cmd));
	if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, uarg, _IOC_SIZE(cmd));

	if (err)
		return -EFAULT;

	switch (cmd) {
	case NVRPC_IOCTL_OPEN:
		err = nvrpc_ioctl_open(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_GET_PORTNAME:
		err = nvrpc_ioctl_get_port_name(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_CLOSE:
		err = nvrpc_ioctl_close(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_INIT:
	case NVRPC_IOCTL_DEINIT:
		break;

	case NVRPC_IOCTL_WAIT_FOR_CONNECT:
		err = nvrpc_ioctl_wait_for_connect(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_CONNECT:
		err = nvrpc_ioctl_connect(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_SET_QUEUE_DEPTH:
		err = nvrpc_ioctl_set_queue_depth(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_SEND_MSG:
		err = nvrpc_ioctl_send_msg(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_SEND_MSG_LP0:
		err = nvrpc_ioctl_send_msg_lp0(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_RECV_MSG:
		err = nvrpc_ioctl_recv_msg(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_XPC_INIT:
		err = nvrpc_ioctl_xpc_init(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_XPC_ACQUIRE:
		err = nvrpc_ioctl_xpc_acquire(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_XPC_RELEASE:
		err = nvrpc_ioctl_xpc_release(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_XPC_GET_MSG:
		err = nvrpc_ioctl_xpc_get_msg(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_XPC_SEND_MSG:
		err = nvrpc_ioctl_xpc_send_msg(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_XPC_DESTROY:
		err = nvrpc_ioctl_xpc_destroy(file, cmd, uarg);
		break;

	case NVRPC_IOCTL_XPC_CREATE:
		err = nvrpc_ioctl_xpc_create(file, cmd, uarg);
		break;

	default:
		return -ENOTTY;
	}
	return err;
}

static int __init nvrpc_init(void)
{
	int ret = 0;

	NvRmDeviceHandle handle;
	NvRmInit(&handle);

	if (s_init_done == NV_FALSE) {
		NvError e;

		e = NvRmOpen(&s_hRmGlobal, 0);
		e = NvRmTransportInit(s_hRmGlobal);
		s_init_done = NV_TRUE;
	}

	ret = misc_register(&nvrpc_dev);
	if (ret) {
		pr_err("%s misc register FAILED\n", __func__);
	}
	return ret;
}

static void __exit nvrpc_deinit(void)
{
	misc_deregister(&nvrpc_dev);
}

module_init(nvrpc_init);
module_exit(nvrpc_deinit);
