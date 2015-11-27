/*
 * PCI I/O adapter configuration related functions.
 *
 * Copyright IBM Corp. 2016
 */
#define KMSG_COMPONENT "sclp_cmd"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/completion.h>
#include <linux/export.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>

#include <asm/sclp.h>

#include "sclp.h"

#define SCLP_CMDW_CONFIGURE_PCI			0x001a0001
#define SCLP_CMDW_DECONFIGURE_PCI		0x001b0001

#define SCLP_RECONFIG_PCI_ATPYE			2

struct pci_cfg_sccb {
	struct sccb_header header;
	u8 atype;		/* adapter type */
	u8 reserved1;
	u16 reserved2;
	u32 aid;		/* adapter identifier */
} __packed;

static int do_pci_configure(sclp_cmdw_t cmd, u32 fid)
{
	struct pci_cfg_sccb *sccb;
	int rc;

	if (!SCLP_HAS_PCI_RECONFIG)
		return -EOPNOTSUPP;

	sccb = (struct pci_cfg_sccb *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!sccb)
		return -ENOMEM;

	sccb->header.length = PAGE_SIZE;
	sccb->atype = SCLP_RECONFIG_PCI_ATPYE;
	sccb->aid = fid;
	rc = sclp_sync_request(cmd, sccb);
	if (rc)
		goto out;
	switch (sccb->header.response_code) {
	case 0x0020:
	case 0x0120:
		break;
	default:
		pr_warn("configure PCI I/O adapter failed: cmd=0x%08x  response=0x%04x\n",
			cmd, sccb->header.response_code);
		rc = -EIO;
		break;
	}
out:
	free_page((unsigned long) sccb);
	return rc;
}

int sclp_pci_configure(u32 fid)
{
	return do_pci_configure(SCLP_CMDW_CONFIGURE_PCI, fid);
}
EXPORT_SYMBOL(sclp_pci_configure);

int sclp_pci_deconfigure(u32 fid)
{
	return do_pci_configure(SCLP_CMDW_DECONFIGURE_PCI, fid);
}
EXPORT_SYMBOL(sclp_pci_deconfigure);
