/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_DRV_H
#define ADF_DRV_H

#include <linux/list.h>
#include <linux/pci.h>
#include "adf_accel_devices.h"
#include "icp_qat_fw_loader_handle.h"
#include "icp_qat_hal.h"

#define ADF_MAJOR_VERSION	0
#define ADF_MINOR_VERSION	6
#define ADF_BUILD_VERSION	0
#define ADF_DRV_VERSION		__stringify(ADF_MAJOR_VERSION) "." \
				__stringify(ADF_MINOR_VERSION) "." \
				__stringify(ADF_BUILD_VERSION)

#define ADF_STATUS_RESTARTING 0
#define ADF_STATUS_STARTING 1
#define ADF_STATUS_CONFIGURED 2
#define ADF_STATUS_STARTED 3
#define ADF_STATUS_AE_INITIALISED 4
#define ADF_STATUS_AE_UCODE_LOADED 5
#define ADF_STATUS_AE_STARTED 6
#define ADF_STATUS_PF_RUNNING 7
#define ADF_STATUS_IRQ_ALLOCATED 8

enum adf_dev_reset_mode {
	ADF_DEV_RESET_ASYNC = 0,
	ADF_DEV_RESET_SYNC
};

enum adf_event {
	ADF_EVENT_INIT = 0,
	ADF_EVENT_START,
	ADF_EVENT_STOP,
	ADF_EVENT_SHUTDOWN,
	ADF_EVENT_RESTARTING,
	ADF_EVENT_RESTARTED,
};

struct service_hndl {
	int (*event_hld)(struct adf_accel_dev *accel_dev,
			 enum adf_event event);
	unsigned long init_status[ADF_DEVS_ARRAY_SIZE];
	unsigned long start_status[ADF_DEVS_ARRAY_SIZE];
	char *name;
	struct list_head list;
};

static inline int get_current_node(void)
{
	return topology_physical_package_id(raw_smp_processor_id());
}

int adf_service_register(struct service_hndl *service);
int adf_service_unregister(struct service_hndl *service);

int adf_dev_init(struct adf_accel_dev *accel_dev);
int adf_dev_start(struct adf_accel_dev *accel_dev);
void adf_dev_stop(struct adf_accel_dev *accel_dev);
void adf_dev_shutdown(struct adf_accel_dev *accel_dev);

int adf_iov_putmsg(struct adf_accel_dev *accel_dev, u32 msg, u8 vf_nr);
void adf_pf2vf_notify_restarting(struct adf_accel_dev *accel_dev);
int adf_enable_vf2pf_comms(struct adf_accel_dev *accel_dev);
void adf_vf2pf_req_hndl(struct adf_accel_vf_info *vf_info);
void adf_devmgr_update_class_index(struct adf_hw_device_data *hw_data);
void adf_clean_vf_map(bool);

int adf_ctl_dev_register(void);
void adf_ctl_dev_unregister(void);
int adf_processes_dev_register(void);
void adf_processes_dev_unregister(void);

int adf_devmgr_add_dev(struct adf_accel_dev *accel_dev,
		       struct adf_accel_dev *pf);
void adf_devmgr_rm_dev(struct adf_accel_dev *accel_dev,
		       struct adf_accel_dev *pf);
struct list_head *adf_devmgr_get_head(void);
struct adf_accel_dev *adf_devmgr_get_dev_by_id(u32 id);
struct adf_accel_dev *adf_devmgr_get_first(void);
struct adf_accel_dev *adf_devmgr_pci_to_accel_dev(struct pci_dev *pci_dev);
int adf_devmgr_verify_id(u32 id);
void adf_devmgr_get_num_dev(u32 *num);
int adf_devmgr_in_reset(struct adf_accel_dev *accel_dev);
int adf_dev_started(struct adf_accel_dev *accel_dev);
int adf_dev_restarting_notify(struct adf_accel_dev *accel_dev);
int adf_dev_restarted_notify(struct adf_accel_dev *accel_dev);
int adf_ae_init(struct adf_accel_dev *accel_dev);
int adf_ae_shutdown(struct adf_accel_dev *accel_dev);
int adf_ae_fw_load(struct adf_accel_dev *accel_dev);
void adf_ae_fw_release(struct adf_accel_dev *accel_dev);
int adf_ae_start(struct adf_accel_dev *accel_dev);
int adf_ae_stop(struct adf_accel_dev *accel_dev);

int adf_enable_aer(struct adf_accel_dev *accel_dev);
void adf_disable_aer(struct adf_accel_dev *accel_dev);
void adf_reset_sbr(struct adf_accel_dev *accel_dev);
void adf_reset_flr(struct adf_accel_dev *accel_dev);
void adf_dev_restore(struct adf_accel_dev *accel_dev);
int adf_init_aer(void);
void adf_exit_aer(void);
int adf_init_admin_comms(struct adf_accel_dev *accel_dev);
void adf_exit_admin_comms(struct adf_accel_dev *accel_dev);
int adf_send_admin_init(struct adf_accel_dev *accel_dev);
int adf_init_arb(struct adf_accel_dev *accel_dev);
void adf_exit_arb(struct adf_accel_dev *accel_dev);
void adf_update_ring_arb(struct adf_etr_ring_data *ring);

int adf_dev_get(struct adf_accel_dev *accel_dev);
void adf_dev_put(struct adf_accel_dev *accel_dev);
int adf_dev_in_use(struct adf_accel_dev *accel_dev);
int adf_init_etr_data(struct adf_accel_dev *accel_dev);
void adf_cleanup_etr_data(struct adf_accel_dev *accel_dev);
int qat_crypto_register(void);
int qat_crypto_unregister(void);
int qat_crypto_dev_config(struct adf_accel_dev *accel_dev);
struct qat_crypto_instance *qat_crypto_get_instance_node(int node);
void qat_crypto_put_instance(struct qat_crypto_instance *inst);
void qat_alg_callback(void *resp);
void qat_alg_asym_callback(void *resp);
int qat_algs_register(void);
void qat_algs_unregister(void);
int qat_asym_algs_register(void);
void qat_asym_algs_unregister(void);

int adf_isr_resource_alloc(struct adf_accel_dev *accel_dev);
void adf_isr_resource_free(struct adf_accel_dev *accel_dev);
int adf_vf_isr_resource_alloc(struct adf_accel_dev *accel_dev);
void adf_vf_isr_resource_free(struct adf_accel_dev *accel_dev);

int qat_hal_init(struct adf_accel_dev *accel_dev);
void qat_hal_deinit(struct icp_qat_fw_loader_handle *handle);
void qat_hal_start(struct icp_qat_fw_loader_handle *handle, unsigned char ae,
		   unsigned int ctx_mask);
void qat_hal_stop(struct icp_qat_fw_loader_handle *handle, unsigned char ae,
		  unsigned int ctx_mask);
void qat_hal_reset(struct icp_qat_fw_loader_handle *handle);
int qat_hal_clr_reset(struct icp_qat_fw_loader_handle *handle);
void qat_hal_set_live_ctx(struct icp_qat_fw_loader_handle *handle,
			  unsigned char ae, unsigned int ctx_mask);
int qat_hal_check_ae_active(struct icp_qat_fw_loader_handle *handle,
			    unsigned int ae);
int qat_hal_set_ae_lm_mode(struct icp_qat_fw_loader_handle *handle,
			   unsigned char ae, enum icp_qat_uof_regtype lm_type,
			   unsigned char mode);
int qat_hal_set_ae_ctx_mode(struct icp_qat_fw_loader_handle *handle,
			    unsigned char ae, unsigned char mode);
int qat_hal_set_ae_nn_mode(struct icp_qat_fw_loader_handle *handle,
			   unsigned char ae, unsigned char mode);
void qat_hal_set_pc(struct icp_qat_fw_loader_handle *handle,
		    unsigned char ae, unsigned int ctx_mask, unsigned int upc);
void qat_hal_wr_uwords(struct icp_qat_fw_loader_handle *handle,
		       unsigned char ae, unsigned int uaddr,
		       unsigned int words_num, u64 *uword);
void qat_hal_wr_umem(struct icp_qat_fw_loader_handle *handle, unsigned char ae,
		     unsigned int uword_addr, unsigned int words_num,
		     unsigned int *data);
int qat_hal_get_ins_num(void);
int qat_hal_batch_wr_lm(struct icp_qat_fw_loader_handle *handle,
			unsigned char ae,
			struct icp_qat_uof_batch_init *lm_init_header);
int qat_hal_init_gpr(struct icp_qat_fw_loader_handle *handle,
		     unsigned char ae, unsigned char ctx_mask,
		     enum icp_qat_uof_regtype reg_type,
		     unsigned short reg_num, unsigned int regdata);
int qat_hal_init_wr_xfer(struct icp_qat_fw_loader_handle *handle,
			 unsigned char ae, unsigned char ctx_mask,
			 enum icp_qat_uof_regtype reg_type,
			 unsigned short reg_num, unsigned int regdata);
int qat_hal_init_rd_xfer(struct icp_qat_fw_loader_handle *handle,
			 unsigned char ae, unsigned char ctx_mask,
			 enum icp_qat_uof_regtype reg_type,
			 unsigned short reg_num, unsigned int regdata);
int qat_hal_init_nn(struct icp_qat_fw_loader_handle *handle,
		    unsigned char ae, unsigned char ctx_mask,
		    unsigned short reg_num, unsigned int regdata);
int qat_hal_wr_lm(struct icp_qat_fw_loader_handle *handle,
		  unsigned char ae, unsigned short lm_addr, unsigned int value);
int qat_uclo_wr_all_uimage(struct icp_qat_fw_loader_handle *handle);
void qat_uclo_del_uof_obj(struct icp_qat_fw_loader_handle *handle);
int qat_uclo_wr_mimage(struct icp_qat_fw_loader_handle *handle, void *addr_ptr,
		       int mem_size);
int qat_uclo_map_obj(struct icp_qat_fw_loader_handle *handle,
		     void *addr_ptr, int mem_size);
#if defined(CONFIG_PCI_IOV)
int adf_sriov_configure(struct pci_dev *pdev, int numvfs);
void adf_disable_sriov(struct adf_accel_dev *accel_dev);
void adf_disable_vf2pf_interrupts(struct adf_accel_dev *accel_dev,
				  u32 vf_mask);
void adf_enable_vf2pf_interrupts(struct adf_accel_dev *accel_dev,
				 u32 vf_mask);
void adf_enable_pf2vf_interrupts(struct adf_accel_dev *accel_dev);
void adf_disable_pf2vf_interrupts(struct adf_accel_dev *accel_dev);

int adf_vf2pf_notify_init(struct adf_accel_dev *accel_dev);
void adf_vf2pf_notify_shutdown(struct adf_accel_dev *accel_dev);
int adf_init_pf_wq(void);
void adf_exit_pf_wq(void);
int adf_init_vf_wq(void);
void adf_exit_vf_wq(void);
#else
static inline int adf_sriov_configure(struct pci_dev *pdev, int numvfs)
{
	return 0;
}

static inline void adf_disable_sriov(struct adf_accel_dev *accel_dev)
{
}

static inline void adf_enable_pf2vf_interrupts(struct adf_accel_dev *accel_dev)
{
}

static inline void adf_disable_pf2vf_interrupts(struct adf_accel_dev *accel_dev)
{
}

static inline int adf_vf2pf_notify_init(struct adf_accel_dev *accel_dev)
{
	return 0;
}

static inline void adf_vf2pf_notify_shutdown(struct adf_accel_dev *accel_dev)
{
}

static inline int adf_init_pf_wq(void)
{
	return 0;
}

static inline void adf_exit_pf_wq(void)
{
}

static inline int adf_init_vf_wq(void)
{
	return 0;
}

static inline void adf_exit_vf_wq(void)
{
}

#endif
#endif
