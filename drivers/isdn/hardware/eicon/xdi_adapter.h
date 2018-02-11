/* SPDX-License-Identifier: GPL-2.0 */
/* $Id: xdi_adapter.h,v 1.7 2004/03/21 17:26:01 armin Exp $ */

#ifndef __DIVA_OS_XDI_ADAPTER_H__
#define __DIVA_OS_XDI_ADAPTER_H__

#define DIVAS_XDI_ADAPTER_BUS_PCI  0
#define DIVAS_XDI_ADAPTER_BUS_ISA  1

typedef struct _divas_pci_card_resources {
	byte bus;
	byte func;
	void *hdev;

	dword bar[8];		/* contains context of appropriate BAR Register */
	void __iomem *addr[8];		/* same bar, but mapped into memory */
	dword length[8];	/* bar length */
	int mem_type_id[MAX_MEM_TYPE];
	unsigned int qoffset;
	byte irq;
} divas_pci_card_resources_t;

typedef union _divas_card_resources {
	divas_pci_card_resources_t pci;
} divas_card_resources_t;

struct _diva_os_xdi_adapter;
typedef int (*diva_init_card_proc_t)(struct _diva_os_xdi_adapter *a);
typedef int (*diva_cmd_card_proc_t)(struct _diva_os_xdi_adapter *a,
				    diva_xdi_um_cfg_cmd_t *data,
				    int length);
typedef void (*diva_xdi_clear_interrupts_proc_t)(struct
						 _diva_os_xdi_adapter *);

#define DIVA_XDI_MBOX_BUSY			1
#define DIVA_XDI_MBOX_WAIT_XLOG	2

typedef struct _xdi_mbox_t {
	dword status;
	diva_xdi_um_cfg_cmd_data_t cmd_data;
	dword data_length;
	void *data;
} xdi_mbox_t;

typedef struct _diva_os_idi_adapter_interface {
	diva_init_card_proc_t cleanup_adapter_proc;
	diva_cmd_card_proc_t cmd_proc;
} diva_os_idi_adapter_interface_t;

typedef struct _diva_os_xdi_adapter {
	struct list_head link;
	int CardIndex;
	int CardOrdinal;
	int controller;		/* number of this controller */
	int Bus;		/* PCI, ISA, ... */
	divas_card_resources_t resources;
	char port_name[24];
	ISDN_ADAPTER xdi_adapter;
	xdi_mbox_t xdi_mbox;
	diva_os_idi_adapter_interface_t interface;
	struct _diva_os_xdi_adapter *slave_adapters[3];
	void *slave_list;
	void *proc_adapter_dir;	/* adapterX proc entry */
	void *proc_info;	/* info proc entry     */
	void *proc_grp_opt;	/* group_optimization  */
	void *proc_d_l1_down;	/* dynamic_l1_down     */
	volatile diva_xdi_clear_interrupts_proc_t clear_interrupts_proc;
	dword dsp_mask;
} diva_os_xdi_adapter_t;

#endif
