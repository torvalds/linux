/*
 * The file intends to implement the platform dependent EEH operations on pseries.
 * Actually, the pseries platform is built based on RTAS heavily. That means the
 * pseries platform dependent EEH operations will be built on RTAS calls. The functions
 * are devired from arch/powerpc/platforms/pseries/eeh.c and necessary cleanup has
 * been done.
 *
 * Copyright Benjamin Herrenschmidt & Gavin Shan, IBM Corporation 2011.
 * Copyright IBM Corporation 2001, 2005, 2006
 * Copyright Dave Engebretsen & Todd Inglett 2001
 * Copyright Linas Vepstas 2005, 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>

#include <asm/eeh.h>
#include <asm/eeh_event.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ppc-pci.h>
#include <asm/rtas.h>

/* RTAS tokens */
static int ibm_set_eeh_option;
static int ibm_set_slot_reset;
static int ibm_read_slot_reset_state;
static int ibm_read_slot_reset_state2;
static int ibm_slot_error_detail;
static int ibm_get_config_addr_info;
static int ibm_get_config_addr_info2;
static int ibm_configure_bridge;
static int ibm_configure_pe;

/**
 * pseries_eeh_init - EEH platform dependent initialization
 *
 * EEH platform dependent initialization on pseries.
 */
static int pseries_eeh_init(void)
{
	/* figure out EEH RTAS function call tokens */
	ibm_set_eeh_option		= rtas_token("ibm,set-eeh-option");
	ibm_set_slot_reset		= rtas_token("ibm,set-slot-reset");
	ibm_read_slot_reset_state2	= rtas_token("ibm,read-slot-reset-state2");
	ibm_read_slot_reset_state	= rtas_token("ibm,read-slot-reset-state");
	ibm_slot_error_detail		= rtas_token("ibm,slot-error-detail");
	ibm_get_config_addr_info2	= rtas_token("ibm,get-config-addr-info2");
	ibm_get_config_addr_info	= rtas_token("ibm,get-config-addr-info");
	ibm_configure_pe		= rtas_token("ibm,configure-pe");
	ibm_configure_bridge		= rtas_token ("ibm,configure-bridge");

	/* necessary sanity check */
	if (ibm_set_eeh_option == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm,set-eeh-option> invalid\n",
			__func__);
		return -EINVAL;
	} else if (ibm_set_slot_reset == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm, set-slot-reset> invalid\n",
			__func__);
		return -EINVAL;
	} else if (ibm_read_slot_reset_state2 == RTAS_UNKNOWN_SERVICE &&
		   ibm_read_slot_reset_state == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm,read-slot-reset-state2> and "
			"<ibm,read-slot-reset-state> invalid\n",
			__func__);
		return -EINVAL;
	} else if (ibm_slot_error_detail == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm,slot-error-detail> invalid\n",
			__func__);
		return -EINVAL;
	} else if (ibm_get_config_addr_info2 == RTAS_UNKNOWN_SERVICE &&
		   ibm_get_config_addr_info == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm,get-config-addr-info2> and "
			"<ibm,get-config-addr-info> invalid\n",
			__func__);
		return -EINVAL;
	} else if (ibm_configure_pe == RTAS_UNKNOWN_SERVICE &&
		   ibm_configure_bridge == RTAS_UNKNOWN_SERVICE) {
		pr_warning("%s: RTAS service <ibm,configure-pe> and "
			"<ibm,configure-bridge> invalid\n",
			__func__);
		return -EINVAL;
	}

	return 0;
}

/**
 * pseries_eeh_set_option - Initialize EEH or MMIO/DMA reenable
 * @dn: device node
 * @option: operation to be issued
 *
 * The function is used to control the EEH functionality globally.
 * Currently, following options are support according to PAPR:
 * Enable EEH, Disable EEH, Enable MMIO and Enable DMA
 */
static int pseries_eeh_set_option(struct device_node *dn, int option)
{
	return 0;
}

/**
 * pseries_eeh_get_pe_addr - Retrieve PE address
 * @dn: device node
 *
 * Retrieve the assocated PE address. Actually, there're 2 RTAS
 * function calls dedicated for the purpose. We need implement
 * it through the new function and then the old one. Besides,
 * you should make sure the config address is figured out from
 * FDT node before calling the function.
 *
 * It's notable that zero'ed return value means invalid PE config
 * address.
 */
static int pseries_eeh_get_pe_addr(struct device_node *dn)
{
	return 0;
}

/**
 * pseries_eeh_get_state - Retrieve PE state
 * @dn: PE associated device node
 * @state: return value
 *
 * Retrieve the state of the specified PE. On RTAS compliant
 * pseries platform, there already has one dedicated RTAS function
 * for the purpose. It's notable that the associated PE config address
 * might be ready when calling the function. Therefore, endeavour to
 * use the PE config address if possible. Further more, there're 2
 * RTAS calls for the purpose, we need to try the new one and back
 * to the old one if the new one couldn't work properly.
 */
static int pseries_eeh_get_state(struct device_node *dn, int *state)
{
	return 0;
}

/**
 * pseries_eeh_reset - Reset the specified PE
 * @dn: PE associated device node
 * @option: reset option
 *
 * Reset the specified PE
 */
static int pseries_eeh_reset(struct device_node *dn, int option)
{
	return 0;
}

/**
 * pseries_eeh_wait_state - Wait for PE state
 * @dn: PE associated device node
 * @max_wait: maximal period in microsecond
 *
 * Wait for the state of associated PE. It might take some time
 * to retrieve the PE's state.
 */
static int pseries_eeh_wait_state(struct device_node *dn, int max_wait)
{
	return 0;
}

/**
 * pseries_eeh_get_log - Retrieve error log
 * @dn: device node
 * @severity: temporary or permanent error log
 * @drv_log: driver log to be combined with retrieved error log
 * @len: length of driver log
 *
 * Retrieve the temporary or permanent error from the PE.
 * Actually, the error will be retrieved through the dedicated
 * RTAS call.
 */
static int pseries_eeh_get_log(struct device_node *dn, int severity, char *drv_log, unsigned long len)
{
	return 0;
}

/**
 * pseries_eeh_configure_bridge - Configure PCI bridges in the indicated PE
 * @dn: PE associated device node
 *
 * The function will be called to reconfigure the bridges included
 * in the specified PE so that the mulfunctional PE would be recovered
 * again.
 */
static int pseries_eeh_configure_bridge(struct device_node *dn)
{
	return 0;
}

static struct eeh_ops pseries_eeh_ops = {
	.name			= "pseries",
	.init			= pseries_eeh_init,
	.set_option		= pseries_eeh_set_option,
	.get_pe_addr		= pseries_eeh_get_pe_addr,
	.get_state		= pseries_eeh_get_state,
	.reset			= pseries_eeh_reset,
	.wait_state		= pseries_eeh_wait_state,
	.get_log		= pseries_eeh_get_log,
	.configure_bridge	= pseries_eeh_configure_bridge
};

/**
 * eeh_pseries_init - Register platform dependent EEH operations
 *
 * EEH initialization on pseries platform. This function should be
 * called before any EEH related functions.
 */
int __init eeh_pseries_init(void)
{
	return eeh_ops_register(&pseries_eeh_ops);
}
