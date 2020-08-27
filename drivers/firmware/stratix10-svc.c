// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017-2018, Intel Corporation
 */

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/kfifo.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/firmware/intel/stratix10-smc.h>
#include <linux/firmware/intel/stratix10-svc-client.h>
#include <linux/types.h>

/**
 * SVC_NUM_DATA_IN_FIFO - number of struct stratix10_svc_data in the FIFO
 *
 * SVC_NUM_CHANNEL - number of channel supported by service layer driver
 *
 * FPGA_CONFIG_DATA_CLAIM_TIMEOUT_MS - claim back the submitted buffer(s)
 * from the secure world for FPGA manager to reuse, or to free the buffer(s)
 * when all bit-stream data had be send.
 *
 * FPGA_CONFIG_STATUS_TIMEOUT_SEC - poll the FPGA configuration status,
 * service layer will return error to FPGA manager when timeout occurs,
 * timeout is set to 30 seconds (30 * 1000) at Intel Stratix10 SoC.
 */
#define SVC_NUM_DATA_IN_FIFO			32
#define SVC_NUM_CHANNEL				2
#define FPGA_CONFIG_DATA_CLAIM_TIMEOUT_MS	200
#define FPGA_CONFIG_STATUS_TIMEOUT_SEC		30

/* stratix10 service layer clients */
#define STRATIX10_RSU				"stratix10-rsu"

typedef void (svc_invoke_fn)(unsigned long, unsigned long, unsigned long,
			     unsigned long, unsigned long, unsigned long,
			     unsigned long, unsigned long,
			     struct arm_smccc_res *);
struct stratix10_svc_chan;

/**
 * struct stratix10_svc - svc private data
 * @stratix10_svc_rsu: pointer to stratix10 RSU device
 */
struct stratix10_svc {
	struct platform_device *stratix10_svc_rsu;
};

/**
 * struct stratix10_svc_sh_memory - service shared memory structure
 * @sync_complete: state for a completion
 * @addr: physical address of shared memory block
 * @size: size of shared memory block
 * @invoke_fn: function to issue secure monitor or hypervisor call
 *
 * This struct is used to save physical address and size of shared memory
 * block. The shared memory blocked is allocated by secure monitor software
 * at secure world.
 *
 * Service layer driver uses the physical address and size to create a memory
 * pool, then allocates data buffer from that memory pool for service client.
 */
struct stratix10_svc_sh_memory {
	struct completion sync_complete;
	unsigned long addr;
	unsigned long size;
	svc_invoke_fn *invoke_fn;
};

/**
 * struct stratix10_svc_data_mem - service memory structure
 * @vaddr: virtual address
 * @paddr: physical address
 * @size: size of memory
 * @node: link list head node
 *
 * This struct is used in a list that keeps track of buffers which have
 * been allocated or freed from the memory pool. Service layer driver also
 * uses this struct to transfer physical address to virtual address.
 */
struct stratix10_svc_data_mem {
	void *vaddr;
	phys_addr_t paddr;
	size_t size;
	struct list_head node;
};

/**
 * struct stratix10_svc_data - service data structure
 * @chan: service channel
 * @paddr: playload physical address
 * @size: playload size
 * @command: service command requested by client
 * @flag: configuration type (full or partial)
 * @arg: args to be passed via registers and not physically mapped buffers
 *
 * This struct is used in service FIFO for inter-process communication.
 */
struct stratix10_svc_data {
	struct stratix10_svc_chan *chan;
	phys_addr_t paddr;
	size_t size;
	u32 command;
	u32 flag;
	u64 arg[3];
};

/**
 * struct stratix10_svc_controller - service controller
 * @dev: device
 * @chans: array of service channels
 * @num_chans: number of channels in 'chans' array
 * @num_active_client: number of active service client
 * @node: list management
 * @genpool: memory pool pointing to the memory region
 * @task: pointer to the thread task which handles SMC or HVC call
 * @svc_fifo: a queue for storing service message data
 * @complete_status: state for completion
 * @svc_fifo_lock: protect access to service message data queue
 * @invoke_fn: function to issue secure monitor call or hypervisor call
 *
 * This struct is used to create communication channels for service clients, to
 * handle secure monitor or hypervisor call.
 */
struct stratix10_svc_controller {
	struct device *dev;
	struct stratix10_svc_chan *chans;
	int num_chans;
	int num_active_client;
	struct list_head node;
	struct gen_pool *genpool;
	struct task_struct *task;
	struct kfifo svc_fifo;
	struct completion complete_status;
	spinlock_t svc_fifo_lock;
	svc_invoke_fn *invoke_fn;
};

/**
 * struct stratix10_svc_chan - service communication channel
 * @ctrl: pointer to service controller which is the provider of this channel
 * @scl: pointer to service client which owns the channel
 * @name: service client name associated with the channel
 * @lock: protect access to the channel
 *
 * This struct is used by service client to communicate with service layer, each
 * service client has its own channel created by service controller.
 */
struct stratix10_svc_chan {
	struct stratix10_svc_controller *ctrl;
	struct stratix10_svc_client *scl;
	char *name;
	spinlock_t lock;
};

static LIST_HEAD(svc_ctrl);
static LIST_HEAD(svc_data_mem);

/**
 * svc_pa_to_va() - translate physical address to virtual address
 * @addr: to be translated physical address
 *
 * Return: valid virtual address or NULL if the provided physical
 * address doesn't exist.
 */
static void *svc_pa_to_va(unsigned long addr)
{
	struct stratix10_svc_data_mem *pmem;

	pr_debug("claim back P-addr=0x%016x\n", (unsigned int)addr);
	list_for_each_entry(pmem, &svc_data_mem, node)
		if (pmem->paddr == addr)
			return pmem->vaddr;

	/* physical address is not found */
	return NULL;
}

/**
 * svc_thread_cmd_data_claim() - claim back buffer from the secure world
 * @ctrl: pointer to service layer controller
 * @p_data: pointer to service data structure
 * @cb_data: pointer to callback data structure to service client
 *
 * Claim back the submitted buffers from the secure world and pass buffer
 * back to service client (FPGA manager, etc) for reuse.
 */
static void svc_thread_cmd_data_claim(struct stratix10_svc_controller *ctrl,
				      struct stratix10_svc_data *p_data,
				      struct stratix10_svc_cb_data *cb_data)
{
	struct arm_smccc_res res;
	unsigned long timeout;

	reinit_completion(&ctrl->complete_status);
	timeout = msecs_to_jiffies(FPGA_CONFIG_DATA_CLAIM_TIMEOUT_MS);

	pr_debug("%s: claim back the submitted buffer\n", __func__);
	do {
		ctrl->invoke_fn(INTEL_SIP_SMC_FPGA_CONFIG_COMPLETED_WRITE,
				0, 0, 0, 0, 0, 0, 0, &res);

		if (res.a0 == INTEL_SIP_SMC_STATUS_OK) {
			if (!res.a1) {
				complete(&ctrl->complete_status);
				break;
			}
			cb_data->status = BIT(SVC_STATUS_BUFFER_DONE);
			cb_data->kaddr1 = svc_pa_to_va(res.a1);
			cb_data->kaddr2 = (res.a2) ?
					  svc_pa_to_va(res.a2) : NULL;
			cb_data->kaddr3 = (res.a3) ?
					  svc_pa_to_va(res.a3) : NULL;
			p_data->chan->scl->receive_cb(p_data->chan->scl,
						      cb_data);
		} else {
			pr_debug("%s: secure world busy, polling again\n",
				 __func__);
		}
	} while (res.a0 == INTEL_SIP_SMC_STATUS_OK ||
		 res.a0 == INTEL_SIP_SMC_STATUS_BUSY ||
		 wait_for_completion_timeout(&ctrl->complete_status, timeout));
}

/**
 * svc_thread_cmd_config_status() - check configuration status
 * @ctrl: pointer to service layer controller
 * @p_data: pointer to service data structure
 * @cb_data: pointer to callback data structure to service client
 *
 * Check whether the secure firmware at secure world has finished the FPGA
 * configuration, and then inform FPGA manager the configuration status.
 */
static void svc_thread_cmd_config_status(struct stratix10_svc_controller *ctrl,
					 struct stratix10_svc_data *p_data,
					 struct stratix10_svc_cb_data *cb_data)
{
	struct arm_smccc_res res;
	int count_in_sec;

	cb_data->kaddr1 = NULL;
	cb_data->kaddr2 = NULL;
	cb_data->kaddr3 = NULL;
	cb_data->status = BIT(SVC_STATUS_ERROR);

	pr_debug("%s: polling config status\n", __func__);

	count_in_sec = FPGA_CONFIG_STATUS_TIMEOUT_SEC;
	while (count_in_sec) {
		ctrl->invoke_fn(INTEL_SIP_SMC_FPGA_CONFIG_ISDONE,
				0, 0, 0, 0, 0, 0, 0, &res);
		if ((res.a0 == INTEL_SIP_SMC_STATUS_OK) ||
		    (res.a0 == INTEL_SIP_SMC_STATUS_ERROR))
			break;

		/*
		 * configuration is still in progress, wait one second then
		 * poll again
		 */
		msleep(1000);
		count_in_sec--;
	}

	if (res.a0 == INTEL_SIP_SMC_STATUS_OK && count_in_sec)
		cb_data->status = BIT(SVC_STATUS_COMPLETED);

	p_data->chan->scl->receive_cb(p_data->chan->scl, cb_data);
}

/**
 * svc_thread_recv_status_ok() - handle the successful status
 * @p_data: pointer to service data structure
 * @cb_data: pointer to callback data structure to service client
 * @res: result from SMC or HVC call
 *
 * Send back the correspond status to the service clients.
 */
static void svc_thread_recv_status_ok(struct stratix10_svc_data *p_data,
				      struct stratix10_svc_cb_data *cb_data,
				      struct arm_smccc_res res)
{
	cb_data->kaddr1 = NULL;
	cb_data->kaddr2 = NULL;
	cb_data->kaddr3 = NULL;

	switch (p_data->command) {
	case COMMAND_RECONFIG:
	case COMMAND_RSU_UPDATE:
	case COMMAND_RSU_NOTIFY:
		cb_data->status = BIT(SVC_STATUS_OK);
		break;
	case COMMAND_RECONFIG_DATA_SUBMIT:
		cb_data->status = BIT(SVC_STATUS_BUFFER_SUBMITTED);
		break;
	case COMMAND_RECONFIG_STATUS:
		cb_data->status = BIT(SVC_STATUS_COMPLETED);
		break;
	case COMMAND_RSU_RETRY:
	case COMMAND_RSU_MAX_RETRY:
		cb_data->status = BIT(SVC_STATUS_OK);
		cb_data->kaddr1 = &res.a1;
		break;
	case COMMAND_RSU_DCMF_VERSION:
		cb_data->status = BIT(SVC_STATUS_OK);
		cb_data->kaddr1 = &res.a1;
		cb_data->kaddr2 = &res.a2;
		break;
	default:
		pr_warn("it shouldn't happen\n");
		break;
	}

	pr_debug("%s: call receive_cb\n", __func__);
	p_data->chan->scl->receive_cb(p_data->chan->scl, cb_data);
}

/**
 * svc_normal_to_secure_thread() - the function to run in the kthread
 * @data: data pointer for kthread function
 *
 * Service layer driver creates stratix10_svc_smc_hvc_call kthread on CPU
 * node 0, its function stratix10_svc_secure_call_thread is used to handle
 * SMC or HVC calls between kernel driver and secure monitor software.
 *
 * Return: 0 for success or -ENOMEM on error.
 */
static int svc_normal_to_secure_thread(void *data)
{
	struct stratix10_svc_controller
			*ctrl = (struct stratix10_svc_controller *)data;
	struct stratix10_svc_data *pdata;
	struct stratix10_svc_cb_data *cbdata;
	struct arm_smccc_res res;
	unsigned long a0, a1, a2;
	int ret_fifo = 0;

	pdata =  kmalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	cbdata = kmalloc(sizeof(*cbdata), GFP_KERNEL);
	if (!cbdata) {
		kfree(pdata);
		return -ENOMEM;
	}

	/* default set, to remove build warning */
	a0 = INTEL_SIP_SMC_FPGA_CONFIG_LOOPBACK;
	a1 = 0;
	a2 = 0;

	pr_debug("smc_hvc_shm_thread is running\n");

	while (!kthread_should_stop()) {
		ret_fifo = kfifo_out_spinlocked(&ctrl->svc_fifo,
						pdata, sizeof(*pdata),
						&ctrl->svc_fifo_lock);

		if (!ret_fifo)
			continue;

		pr_debug("get from FIFO pa=0x%016x, command=%u, size=%u\n",
			 (unsigned int)pdata->paddr, pdata->command,
			 (unsigned int)pdata->size);

		switch (pdata->command) {
		case COMMAND_RECONFIG_DATA_CLAIM:
			svc_thread_cmd_data_claim(ctrl, pdata, cbdata);
			continue;
		case COMMAND_RECONFIG:
			a0 = INTEL_SIP_SMC_FPGA_CONFIG_START;
			pr_debug("conf_type=%u\n", (unsigned int)pdata->flag);
			a1 = pdata->flag;
			a2 = 0;
			break;
		case COMMAND_RECONFIG_DATA_SUBMIT:
			a0 = INTEL_SIP_SMC_FPGA_CONFIG_WRITE;
			a1 = (unsigned long)pdata->paddr;
			a2 = (unsigned long)pdata->size;
			break;
		case COMMAND_RECONFIG_STATUS:
			a0 = INTEL_SIP_SMC_FPGA_CONFIG_ISDONE;
			a1 = 0;
			a2 = 0;
			break;
		case COMMAND_RSU_STATUS:
			a0 = INTEL_SIP_SMC_RSU_STATUS;
			a1 = 0;
			a2 = 0;
			break;
		case COMMAND_RSU_UPDATE:
			a0 = INTEL_SIP_SMC_RSU_UPDATE;
			a1 = pdata->arg[0];
			a2 = 0;
			break;
		case COMMAND_RSU_NOTIFY:
			a0 = INTEL_SIP_SMC_RSU_NOTIFY;
			a1 = pdata->arg[0];
			a2 = 0;
			break;
		case COMMAND_RSU_RETRY:
			a0 = INTEL_SIP_SMC_RSU_RETRY_COUNTER;
			a1 = 0;
			a2 = 0;
			break;
		case COMMAND_RSU_MAX_RETRY:
			a0 = INTEL_SIP_SMC_RSU_MAX_RETRY;
			a1 = 0;
			a2 = 0;
			break;
		case COMMAND_RSU_DCMF_VERSION:
			a0 = INTEL_SIP_SMC_RSU_DCMF_VERSION;
			a1 = 0;
			a2 = 0;
			break;
		default:
			pr_warn("it shouldn't happen\n");
			break;
		}
		pr_debug("%s: before SMC call -- a0=0x%016x a1=0x%016x",
			 __func__, (unsigned int)a0, (unsigned int)a1);
		pr_debug(" a2=0x%016x\n", (unsigned int)a2);

		ctrl->invoke_fn(a0, a1, a2, 0, 0, 0, 0, 0, &res);

		pr_debug("%s: after SMC call -- res.a0=0x%016x",
			 __func__, (unsigned int)res.a0);
		pr_debug(" res.a1=0x%016x, res.a2=0x%016x",
			 (unsigned int)res.a1, (unsigned int)res.a2);
		pr_debug(" res.a3=0x%016x\n", (unsigned int)res.a3);

		if (pdata->command == COMMAND_RSU_STATUS) {
			if (res.a0 == INTEL_SIP_SMC_RSU_ERROR)
				cbdata->status = BIT(SVC_STATUS_ERROR);
			else
				cbdata->status = BIT(SVC_STATUS_OK);

			cbdata->kaddr1 = &res;
			cbdata->kaddr2 = NULL;
			cbdata->kaddr3 = NULL;
			pdata->chan->scl->receive_cb(pdata->chan->scl, cbdata);
			continue;
		}

		switch (res.a0) {
		case INTEL_SIP_SMC_STATUS_OK:
			svc_thread_recv_status_ok(pdata, cbdata, res);
			break;
		case INTEL_SIP_SMC_STATUS_BUSY:
			switch (pdata->command) {
			case COMMAND_RECONFIG_DATA_SUBMIT:
				svc_thread_cmd_data_claim(ctrl,
							  pdata, cbdata);
				break;
			case COMMAND_RECONFIG_STATUS:
				svc_thread_cmd_config_status(ctrl,
							     pdata, cbdata);
				break;
			default:
				pr_warn("it shouldn't happen\n");
				break;
			}
			break;
		case INTEL_SIP_SMC_STATUS_REJECTED:
			pr_debug("%s: STATUS_REJECTED\n", __func__);
			break;
		case INTEL_SIP_SMC_STATUS_ERROR:
		case INTEL_SIP_SMC_RSU_ERROR:
			pr_err("%s: STATUS_ERROR\n", __func__);
			cbdata->status = BIT(SVC_STATUS_ERROR);
			cbdata->kaddr1 = NULL;
			cbdata->kaddr2 = NULL;
			cbdata->kaddr3 = NULL;
			pdata->chan->scl->receive_cb(pdata->chan->scl, cbdata);
			break;
		default:
			pr_warn("Secure firmware doesn't support...\n");

			/*
			 * be compatible with older version firmware which
			 * doesn't support RSU notify or retry
			 */
			if ((pdata->command == COMMAND_RSU_RETRY) ||
			    (pdata->command == COMMAND_RSU_MAX_RETRY) ||
				(pdata->command == COMMAND_RSU_NOTIFY)) {
				cbdata->status =
					BIT(SVC_STATUS_NO_SUPPORT);
				cbdata->kaddr1 = NULL;
				cbdata->kaddr2 = NULL;
				cbdata->kaddr3 = NULL;
				pdata->chan->scl->receive_cb(
					pdata->chan->scl, cbdata);
			}
			break;

		}
	}

	kfree(cbdata);
	kfree(pdata);

	return 0;
}

/**
 * svc_normal_to_secure_shm_thread() - the function to run in the kthread
 * @data: data pointer for kthread function
 *
 * Service layer driver creates stratix10_svc_smc_hvc_shm kthread on CPU
 * node 0, its function stratix10_svc_secure_shm_thread is used to query the
 * physical address of memory block reserved by secure monitor software at
 * secure world.
 *
 * svc_normal_to_secure_shm_thread() calls do_exit() directly since it is a
 * standlone thread for which no one will call kthread_stop() or return when
 * 'kthread_should_stop()' is true.
 */
static int svc_normal_to_secure_shm_thread(void *data)
{
	struct stratix10_svc_sh_memory
			*sh_mem = (struct stratix10_svc_sh_memory *)data;
	struct arm_smccc_res res;

	/* SMC or HVC call to get shared memory info from secure world */
	sh_mem->invoke_fn(INTEL_SIP_SMC_FPGA_CONFIG_GET_MEM,
			  0, 0, 0, 0, 0, 0, 0, &res);
	if (res.a0 == INTEL_SIP_SMC_STATUS_OK) {
		sh_mem->addr = res.a1;
		sh_mem->size = res.a2;
	} else {
		pr_err("%s: after SMC call -- res.a0=0x%016x",  __func__,
		       (unsigned int)res.a0);
		sh_mem->addr = 0;
		sh_mem->size = 0;
	}

	complete(&sh_mem->sync_complete);
	do_exit(0);
}

/**
 * svc_get_sh_memory() - get memory block reserved by secure monitor SW
 * @pdev: pointer to service layer device
 * @sh_memory: pointer to service shared memory structure
 *
 * Return: zero for successfully getting the physical address of memory block
 * reserved by secure monitor software, or negative value on error.
 */
static int svc_get_sh_memory(struct platform_device *pdev,
				    struct stratix10_svc_sh_memory *sh_memory)
{
	struct device *dev = &pdev->dev;
	struct task_struct *sh_memory_task;
	unsigned int cpu = 0;

	init_completion(&sh_memory->sync_complete);

	/* smc or hvc call happens on cpu 0 bound kthread */
	sh_memory_task = kthread_create_on_node(svc_normal_to_secure_shm_thread,
					       (void *)sh_memory,
						cpu_to_node(cpu),
						"svc_smc_hvc_shm_thread");
	if (IS_ERR(sh_memory_task)) {
		dev_err(dev, "fail to create stratix10_svc_smc_shm_thread\n");
		return -EINVAL;
	}

	wake_up_process(sh_memory_task);

	if (!wait_for_completion_timeout(&sh_memory->sync_complete, 10 * HZ)) {
		dev_err(dev,
			"timeout to get sh-memory paras from secure world\n");
		return -ETIMEDOUT;
	}

	if (!sh_memory->addr || !sh_memory->size) {
		dev_err(dev,
			"failed to get shared memory info from secure world\n");
		return -ENOMEM;
	}

	dev_dbg(dev, "SM software provides paddr: 0x%016x, size: 0x%08x\n",
		(unsigned int)sh_memory->addr,
		(unsigned int)sh_memory->size);

	return 0;
}

/**
 * svc_create_memory_pool() - create a memory pool from reserved memory block
 * @pdev: pointer to service layer device
 * @sh_memory: pointer to service shared memory structure
 *
 * Return: pool allocated from reserved memory block or ERR_PTR() on error.
 */
static struct gen_pool *
svc_create_memory_pool(struct platform_device *pdev,
		       struct stratix10_svc_sh_memory *sh_memory)
{
	struct device *dev = &pdev->dev;
	struct gen_pool *genpool;
	unsigned long vaddr;
	phys_addr_t paddr;
	size_t size;
	phys_addr_t begin;
	phys_addr_t end;
	void *va;
	size_t page_mask = PAGE_SIZE - 1;
	int min_alloc_order = 3;
	int ret;

	begin = roundup(sh_memory->addr, PAGE_SIZE);
	end = rounddown(sh_memory->addr + sh_memory->size, PAGE_SIZE);
	paddr = begin;
	size = end - begin;
	va = memremap(paddr, size, MEMREMAP_WC);
	if (!va) {
		dev_err(dev, "fail to remap shared memory\n");
		return ERR_PTR(-EINVAL);
	}
	vaddr = (unsigned long)va;
	dev_dbg(dev,
		"reserved memory vaddr: %p, paddr: 0x%16x size: 0x%8x\n",
		va, (unsigned int)paddr, (unsigned int)size);
	if ((vaddr & page_mask) || (paddr & page_mask) ||
	    (size & page_mask)) {
		dev_err(dev, "page is not aligned\n");
		return ERR_PTR(-EINVAL);
	}
	genpool = gen_pool_create(min_alloc_order, -1);
	if (!genpool) {
		dev_err(dev, "fail to create genpool\n");
		return ERR_PTR(-ENOMEM);
	}
	gen_pool_set_algo(genpool, gen_pool_best_fit, NULL);
	ret = gen_pool_add_virt(genpool, vaddr, paddr, size, -1);
	if (ret) {
		dev_err(dev, "fail to add memory chunk to the pool\n");
		gen_pool_destroy(genpool);
		return ERR_PTR(ret);
	}

	return genpool;
}

/**
 * svc_smccc_smc() - secure monitor call between normal and secure world
 * @a0: argument passed in registers 0
 * @a1: argument passed in registers 1
 * @a2: argument passed in registers 2
 * @a3: argument passed in registers 3
 * @a4: argument passed in registers 4
 * @a5: argument passed in registers 5
 * @a6: argument passed in registers 6
 * @a7: argument passed in registers 7
 * @res: result values from register 0 to 3
 */
static void svc_smccc_smc(unsigned long a0, unsigned long a1,
			  unsigned long a2, unsigned long a3,
			  unsigned long a4, unsigned long a5,
			  unsigned long a6, unsigned long a7,
			  struct arm_smccc_res *res)
{
	arm_smccc_smc(a0, a1, a2, a3, a4, a5, a6, a7, res);
}

/**
 * svc_smccc_hvc() - hypervisor call between normal and secure world
 * @a0: argument passed in registers 0
 * @a1: argument passed in registers 1
 * @a2: argument passed in registers 2
 * @a3: argument passed in registers 3
 * @a4: argument passed in registers 4
 * @a5: argument passed in registers 5
 * @a6: argument passed in registers 6
 * @a7: argument passed in registers 7
 * @res: result values from register 0 to 3
 */
static void svc_smccc_hvc(unsigned long a0, unsigned long a1,
			  unsigned long a2, unsigned long a3,
			  unsigned long a4, unsigned long a5,
			  unsigned long a6, unsigned long a7,
			  struct arm_smccc_res *res)
{
	arm_smccc_hvc(a0, a1, a2, a3, a4, a5, a6, a7, res);
}

/**
 * get_invoke_func() - invoke SMC or HVC call
 * @dev: pointer to device
 *
 * Return: function pointer to svc_smccc_smc or svc_smccc_hvc.
 */
static svc_invoke_fn *get_invoke_func(struct device *dev)
{
	const char *method;

	if (of_property_read_string(dev->of_node, "method", &method)) {
		dev_warn(dev, "missing \"method\" property\n");
		return ERR_PTR(-ENXIO);
	}

	if (!strcmp(method, "smc"))
		return svc_smccc_smc;
	if (!strcmp(method, "hvc"))
		return svc_smccc_hvc;

	dev_warn(dev, "invalid \"method\" property: %s\n", method);

	return ERR_PTR(-EINVAL);
}

/**
 * stratix10_svc_request_channel_byname() - request a service channel
 * @client: pointer to service client
 * @name: service client name
 *
 * This function is used by service client to request a service channel.
 *
 * Return: a pointer to channel assigned to the client on success,
 * or ERR_PTR() on error.
 */
struct stratix10_svc_chan *stratix10_svc_request_channel_byname(
	struct stratix10_svc_client *client, const char *name)
{
	struct device *dev = client->dev;
	struct stratix10_svc_controller *controller;
	struct stratix10_svc_chan *chan = NULL;
	unsigned long flag;
	int i;

	/* if probe was called after client's, or error on probe */
	if (list_empty(&svc_ctrl))
		return ERR_PTR(-EPROBE_DEFER);

	controller = list_first_entry(&svc_ctrl,
				      struct stratix10_svc_controller, node);
	for (i = 0; i < SVC_NUM_CHANNEL; i++) {
		if (!strcmp(controller->chans[i].name, name)) {
			chan = &controller->chans[i];
			break;
		}
	}

	/* if there was no channel match */
	if (i == SVC_NUM_CHANNEL) {
		dev_err(dev, "%s: channel not allocated\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	if (chan->scl || !try_module_get(controller->dev->driver->owner)) {
		dev_dbg(dev, "%s: svc not free\n", __func__);
		return ERR_PTR(-EBUSY);
	}

	spin_lock_irqsave(&chan->lock, flag);
	chan->scl = client;
	chan->ctrl->num_active_client++;
	spin_unlock_irqrestore(&chan->lock, flag);

	return chan;
}
EXPORT_SYMBOL_GPL(stratix10_svc_request_channel_byname);

/**
 * stratix10_svc_free_channel() - free service channel
 * @chan: service channel to be freed
 *
 * This function is used by service client to free a service channel.
 */
void stratix10_svc_free_channel(struct stratix10_svc_chan *chan)
{
	unsigned long flag;

	spin_lock_irqsave(&chan->lock, flag);
	chan->scl = NULL;
	chan->ctrl->num_active_client--;
	module_put(chan->ctrl->dev->driver->owner);
	spin_unlock_irqrestore(&chan->lock, flag);
}
EXPORT_SYMBOL_GPL(stratix10_svc_free_channel);

/**
 * stratix10_svc_send() - send a message data to the remote
 * @chan: service channel assigned to the client
 * @msg: message data to be sent, in the format of
 * "struct stratix10_svc_client_msg"
 *
 * This function is used by service client to add a message to the service
 * layer driver's queue for being sent to the secure world.
 *
 * Return: 0 for success, -ENOMEM or -ENOBUFS on error.
 */
int stratix10_svc_send(struct stratix10_svc_chan *chan, void *msg)
{
	struct stratix10_svc_client_msg
		*p_msg = (struct stratix10_svc_client_msg *)msg;
	struct stratix10_svc_data_mem *p_mem;
	struct stratix10_svc_data *p_data;
	int ret = 0;
	unsigned int cpu = 0;

	p_data = kzalloc(sizeof(*p_data), GFP_KERNEL);
	if (!p_data)
		return -ENOMEM;

	/* first client will create kernel thread */
	if (!chan->ctrl->task) {
		chan->ctrl->task =
			kthread_create_on_node(svc_normal_to_secure_thread,
					      (void *)chan->ctrl,
					      cpu_to_node(cpu),
					      "svc_smc_hvc_thread");
			if (IS_ERR(chan->ctrl->task)) {
				dev_err(chan->ctrl->dev,
					"failed to create svc_smc_hvc_thread\n");
				kfree(p_data);
				return -EINVAL;
			}
		kthread_bind(chan->ctrl->task, cpu);
		wake_up_process(chan->ctrl->task);
	}

	pr_debug("%s: sent P-va=%p, P-com=%x, P-size=%u\n", __func__,
		 p_msg->payload, p_msg->command,
		 (unsigned int)p_msg->payload_length);

	if (list_empty(&svc_data_mem)) {
		if (p_msg->command == COMMAND_RECONFIG) {
			struct stratix10_svc_command_config_type *ct =
				(struct stratix10_svc_command_config_type *)
				p_msg->payload;
			p_data->flag = ct->flags;
		}
	} else {
		list_for_each_entry(p_mem, &svc_data_mem, node)
			if (p_mem->vaddr == p_msg->payload) {
				p_data->paddr = p_mem->paddr;
				break;
			}
	}

	p_data->command = p_msg->command;
	p_data->arg[0] = p_msg->arg[0];
	p_data->arg[1] = p_msg->arg[1];
	p_data->arg[2] = p_msg->arg[2];
	p_data->size = p_msg->payload_length;
	p_data->chan = chan;
	pr_debug("%s: put to FIFO pa=0x%016x, cmd=%x, size=%u\n", __func__,
	       (unsigned int)p_data->paddr, p_data->command,
	       (unsigned int)p_data->size);
	ret = kfifo_in_spinlocked(&chan->ctrl->svc_fifo, p_data,
				  sizeof(*p_data),
				  &chan->ctrl->svc_fifo_lock);

	kfree(p_data);

	if (!ret)
		return -ENOBUFS;

	return 0;
}
EXPORT_SYMBOL_GPL(stratix10_svc_send);

/**
 * stratix10_svc_done() - complete service request transactions
 * @chan: service channel assigned to the client
 *
 * This function should be called when client has finished its request
 * or there is an error in the request process. It allows the service layer
 * to stop the running thread to have maximize savings in kernel resources.
 */
void stratix10_svc_done(struct stratix10_svc_chan *chan)
{
	/* stop thread when thread is running AND only one active client */
	if (chan->ctrl->task && chan->ctrl->num_active_client <= 1) {
		pr_debug("svc_smc_hvc_shm_thread is stopped\n");
		kthread_stop(chan->ctrl->task);
		chan->ctrl->task = NULL;
	}
}
EXPORT_SYMBOL_GPL(stratix10_svc_done);

/**
 * stratix10_svc_allocate_memory() - allocate memory
 * @chan: service channel assigned to the client
 * @size: memory size requested by a specific service client
 *
 * Service layer allocates the requested number of bytes buffer from the
 * memory pool, service client uses this function to get allocated buffers.
 *
 * Return: address of allocated memory on success, or ERR_PTR() on error.
 */
void *stratix10_svc_allocate_memory(struct stratix10_svc_chan *chan,
				    size_t size)
{
	struct stratix10_svc_data_mem *pmem;
	unsigned long va;
	phys_addr_t pa;
	struct gen_pool *genpool = chan->ctrl->genpool;
	size_t s = roundup(size, 1 << genpool->min_alloc_order);

	pmem = devm_kzalloc(chan->ctrl->dev, sizeof(*pmem), GFP_KERNEL);
	if (!pmem)
		return ERR_PTR(-ENOMEM);

	va = gen_pool_alloc(genpool, s);
	if (!va)
		return ERR_PTR(-ENOMEM);

	memset((void *)va, 0, s);
	pa = gen_pool_virt_to_phys(genpool, va);

	pmem->vaddr = (void *)va;
	pmem->paddr = pa;
	pmem->size = s;
	list_add_tail(&pmem->node, &svc_data_mem);
	pr_debug("%s: va=%p, pa=0x%016x\n", __func__,
		 pmem->vaddr, (unsigned int)pmem->paddr);

	return (void *)va;
}
EXPORT_SYMBOL_GPL(stratix10_svc_allocate_memory);

/**
 * stratix10_svc_free_memory() - free allocated memory
 * @chan: service channel assigned to the client
 * @kaddr: memory to be freed
 *
 * This function is used by service client to free allocated buffers.
 */
void stratix10_svc_free_memory(struct stratix10_svc_chan *chan, void *kaddr)
{
	struct stratix10_svc_data_mem *pmem;
	size_t size = 0;

	list_for_each_entry(pmem, &svc_data_mem, node)
		if (pmem->vaddr == kaddr) {
			size = pmem->size;
			break;
		}

	gen_pool_free(chan->ctrl->genpool, (unsigned long)kaddr, size);
	pmem->vaddr = NULL;
	list_del(&pmem->node);
}
EXPORT_SYMBOL_GPL(stratix10_svc_free_memory);

static const struct of_device_id stratix10_svc_drv_match[] = {
	{.compatible = "intel,stratix10-svc"},
	{.compatible = "intel,agilex-svc"},
	{},
};

static int stratix10_svc_drv_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stratix10_svc_controller *controller;
	struct stratix10_svc_chan *chans;
	struct gen_pool *genpool;
	struct stratix10_svc_sh_memory *sh_memory;
	struct stratix10_svc *svc;

	svc_invoke_fn *invoke_fn;
	size_t fifo_size;
	int ret;

	/* get SMC or HVC function */
	invoke_fn = get_invoke_func(dev);
	if (IS_ERR(invoke_fn))
		return -EINVAL;

	sh_memory = devm_kzalloc(dev, sizeof(*sh_memory), GFP_KERNEL);
	if (!sh_memory)
		return -ENOMEM;

	sh_memory->invoke_fn = invoke_fn;
	ret = svc_get_sh_memory(pdev, sh_memory);
	if (ret)
		return ret;

	genpool = svc_create_memory_pool(pdev, sh_memory);
	if (!genpool)
		return -ENOMEM;

	/* allocate service controller and supporting channel */
	controller = devm_kzalloc(dev, sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return -ENOMEM;

	chans = devm_kmalloc_array(dev, SVC_NUM_CHANNEL,
				   sizeof(*chans), GFP_KERNEL | __GFP_ZERO);
	if (!chans)
		return -ENOMEM;

	controller->dev = dev;
	controller->num_chans = SVC_NUM_CHANNEL;
	controller->num_active_client = 0;
	controller->chans = chans;
	controller->genpool = genpool;
	controller->task = NULL;
	controller->invoke_fn = invoke_fn;
	init_completion(&controller->complete_status);

	fifo_size = sizeof(struct stratix10_svc_data) * SVC_NUM_DATA_IN_FIFO;
	ret = kfifo_alloc(&controller->svc_fifo, fifo_size, GFP_KERNEL);
	if (ret) {
		dev_err(dev, "failed to allocate FIFO\n");
		return ret;
	}
	spin_lock_init(&controller->svc_fifo_lock);

	chans[0].scl = NULL;
	chans[0].ctrl = controller;
	chans[0].name = SVC_CLIENT_FPGA;
	spin_lock_init(&chans[0].lock);

	chans[1].scl = NULL;
	chans[1].ctrl = controller;
	chans[1].name = SVC_CLIENT_RSU;
	spin_lock_init(&chans[1].lock);

	list_add_tail(&controller->node, &svc_ctrl);
	platform_set_drvdata(pdev, controller);

	/* add svc client device(s) */
	svc = devm_kzalloc(dev, sizeof(*svc), GFP_KERNEL);
	if (!svc)
		return -ENOMEM;

	svc->stratix10_svc_rsu = platform_device_alloc(STRATIX10_RSU, 0);
	if (!svc->stratix10_svc_rsu) {
		dev_err(dev, "failed to allocate %s device\n", STRATIX10_RSU);
		return -ENOMEM;
	}

	ret = platform_device_add(svc->stratix10_svc_rsu);
	if (ret) {
		platform_device_put(svc->stratix10_svc_rsu);
		return ret;
	}
	dev_set_drvdata(dev, svc);

	pr_info("Intel Service Layer Driver Initialized\n");

	return ret;
}

static int stratix10_svc_drv_remove(struct platform_device *pdev)
{
	struct stratix10_svc *svc = dev_get_drvdata(&pdev->dev);
	struct stratix10_svc_controller *ctrl = platform_get_drvdata(pdev);

	platform_device_unregister(svc->stratix10_svc_rsu);

	kfifo_free(&ctrl->svc_fifo);
	if (ctrl->task) {
		kthread_stop(ctrl->task);
		ctrl->task = NULL;
	}
	if (ctrl->genpool)
		gen_pool_destroy(ctrl->genpool);
	list_del(&ctrl->node);

	return 0;
}

static struct platform_driver stratix10_svc_driver = {
	.probe = stratix10_svc_drv_probe,
	.remove = stratix10_svc_drv_remove,
	.driver = {
		.name = "stratix10-svc",
		.of_match_table = stratix10_svc_drv_match,
	},
};

static int __init stratix10_svc_init(void)
{
	struct device_node *fw_np;
	struct device_node *np;
	int ret;

	fw_np = of_find_node_by_name(NULL, "firmware");
	if (!fw_np)
		return -ENODEV;

	np = of_find_matching_node(fw_np, stratix10_svc_drv_match);
	if (!np)
		return -ENODEV;

	of_node_put(np);
	ret = of_platform_populate(fw_np, stratix10_svc_drv_match, NULL, NULL);
	if (ret)
		return ret;

	return platform_driver_register(&stratix10_svc_driver);
}

static void __exit stratix10_svc_exit(void)
{
	return platform_driver_unregister(&stratix10_svc_driver);
}

subsys_initcall(stratix10_svc_init);
module_exit(stratix10_svc_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel Stratix10 Service Layer Driver");
MODULE_AUTHOR("Richard Gong <richard.gong@intel.com>");
MODULE_ALIAS("platform:stratix10-svc");
