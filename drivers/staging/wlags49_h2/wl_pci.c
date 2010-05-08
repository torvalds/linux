/*******************************************************************************
 * Agere Systems Inc.
 * Wireless device driver for Linux (wlags49).
 *
 * Copyright (c) 1998-2003 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 * Initially developed by TriplePoint, Inc.
 *   http://www.triplepoint.com
 *
 *------------------------------------------------------------------------------
 *
 *   This file contains processing and initialization specific to PCI/miniPCI
 *   devices.
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2003 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 ******************************************************************************/

/*******************************************************************************
 * include files
 ******************************************************************************/
#include <wireless/wl_version.h>

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/ctype.h>
#include <linux/string.h>
//#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>

#include <hcf/debug.h>

#include <hcf.h>
#include <dhf.h>
#include <hcfdef.h>

#include <wireless/wl_if.h>
#include <wireless/wl_internal.h>
#include <wireless/wl_util.h>
#include <wireless/wl_main.h>
#include <wireless/wl_netdev.h>
#include <wireless/wl_pci.h>


/*******************************************************************************
 * global variables
 ******************************************************************************/
#if DBG
extern dbg_info_t *DbgInfo;
#endif  // DBG

/* define the PCI device Table Cardname and id tables */
enum hermes_pci_versions {
	CH_Agere_Systems_Mini_PCI_V1 = 0,
};

static struct pci_device_id wl_pci_tbl[] __devinitdata = {
	{ WL_LKM_PCI_VENDOR_ID, WL_LKM_PCI_DEVICE_ID_0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_Agere_Systems_Mini_PCI_V1 },
    { WL_LKM_PCI_VENDOR_ID, WL_LKM_PCI_DEVICE_ID_1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_Agere_Systems_Mini_PCI_V1 },
    { WL_LKM_PCI_VENDOR_ID, WL_LKM_PCI_DEVICE_ID_2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CH_Agere_Systems_Mini_PCI_V1 },
	{ }			/* Terminating entry */
};

MODULE_DEVICE_TABLE(pci, wl_pci_tbl);

/*******************************************************************************
 * function prototypes
 ******************************************************************************/
int __devinit wl_pci_probe( struct pci_dev *pdev,
                                const struct pci_device_id *ent );
void __devexit wl_pci_remove(struct pci_dev *pdev);
int wl_pci_setup( struct pci_dev *pdev );
void wl_pci_enable_cardbus_interrupts( struct pci_dev *pdev );

#ifdef ENABLE_DMA
int wl_pci_dma_alloc( struct pci_dev *pdev, struct wl_private *lp );
int wl_pci_dma_free( struct pci_dev *pdev, struct wl_private *lp );
int wl_pci_dma_alloc_tx_packet( struct pci_dev *pdev, struct wl_private *lp,
                                DESC_STRCT **desc );
int wl_pci_dma_free_tx_packet( struct pci_dev *pdev, struct wl_private *lp,
                                DESC_STRCT **desc );
int wl_pci_dma_alloc_rx_packet( struct pci_dev *pdev, struct wl_private *lp,
                                DESC_STRCT **desc );
int wl_pci_dma_free_rx_packet( struct pci_dev *pdev, struct wl_private *lp,
                                DESC_STRCT **desc );
int wl_pci_dma_alloc_desc_and_buf( struct pci_dev *pdev, struct wl_private *lp,
                                   DESC_STRCT **desc, int size );
int wl_pci_dma_free_desc_and_buf( struct pci_dev *pdev, struct wl_private *lp,
                                   DESC_STRCT **desc );
int wl_pci_dma_alloc_desc( struct pci_dev *pdev, struct wl_private *lp,
                           DESC_STRCT **desc );
int wl_pci_dma_free_desc( struct pci_dev *pdev, struct wl_private *lp,
                           DESC_STRCT **desc );
int wl_pci_dma_alloc_buf( struct pci_dev *pdev, struct wl_private *lp,
                          DESC_STRCT *desc, int size );
int wl_pci_dma_free_buf( struct pci_dev *pdev, struct wl_private *lp,
                          DESC_STRCT *desc );

void wl_pci_dma_hcf_reclaim_rx( struct wl_private *lp );
#endif  // ENABLE_DMA

/*******************************************************************************
 * PCI module function registration
 ******************************************************************************/
static struct pci_driver wl_driver =
{
	name:		MODULE_NAME,
    id_table:	wl_pci_tbl,
	probe:		wl_pci_probe,
	remove:		__devexit_p(wl_pci_remove),
    suspend:    NULL,
    resume:     NULL,
};

/*******************************************************************************
 *	wl_adapter_init_module()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Called by init_module() to perform PCI-specific driver initialization.
 *
 *  PARAMETERS:
 *
 *      N/A
 *
 *  RETURNS:
 *
 *      0
 *
 ******************************************************************************/
int wl_adapter_init_module( void )
{
    int result;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_adapter_init_module()" );
    DBG_ENTER( DbgInfo );
    DBG_TRACE( DbgInfo, "wl_adapter_init_module() -- PCI\n" );

    result = pci_register_driver( &wl_driver ); //;?replace with pci_module_init, Rubini pg 490
	//;? why not do something with the result

    DBG_LEAVE( DbgInfo );
    return 0;
} // wl_adapter_init_module
/*============================================================================*/

/*******************************************************************************
 *	wl_adapter_cleanup_module()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Called by cleanup_module() to perform PCI-specific driver cleanup.
 *
 *  PARAMETERS:
 *
 *      N/A
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_adapter_cleanup_module( void )
{
	//;?how comes wl_adapter_cleanup_module is located in a seemingly pci specific module
    DBG_FUNC( "wl_adapter_cleanup_module" );
    DBG_ENTER( DbgInfo );

	//;?DBG_TRACE below feels like nearly redundant in the light of DBG_ENTER above
    DBG_TRACE( DbgInfo, "wl_adapter_cleanup_module() -- PCI\n" );

    pci_unregister_driver( &wl_driver );

    DBG_LEAVE( DbgInfo );
    return;
} // wl_adapter_cleanup_module
/*============================================================================*/

/*******************************************************************************
 *	wl_adapter_insert()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Called by wl_pci_probe() to continue the process of device insertion.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure
 *
 *  RETURNS:
 *
 *      TRUE or FALSE
 *
 ******************************************************************************/
int wl_adapter_insert( struct net_device *dev )
{
    int result = FALSE;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_adapter_insert" );
    DBG_ENTER( DbgInfo );

    DBG_TRACE( DbgInfo, "wl_adapter_insert() -- PCI\n" );

    if( dev == NULL ) {
        DBG_ERROR( DbgInfo, "net_device pointer is NULL!!!\n" );
    } else if( dev->priv == NULL ) {
        DBG_ERROR( DbgInfo, "wl_private pointer is NULL!!!\n" );
    } else if( wl_insert( dev ) ) { /* Perform remaining device initialization */
		result = TRUE;
	} else {
        DBG_TRACE( DbgInfo, "wl_insert() FAILED\n" );
    }
    DBG_LEAVE( DbgInfo );
    return result;
} // wl_adapter_insert
/*============================================================================*/

/*******************************************************************************
 *	wl_adapter_open()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Open the device.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure
 *
 *  RETURNS:
 *
 *      an HCF status code
 *
 ******************************************************************************/
int wl_adapter_open( struct net_device *dev )
{
    int         result = 0;
    int         hcf_status = HCF_SUCCESS;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_adapter_open" );
    DBG_ENTER( DbgInfo );

    DBG_TRACE( DbgInfo, "wl_adapter_open() -- PCI\n" );

    hcf_status = wl_open( dev );

    if( hcf_status != HCF_SUCCESS ) {
        result = -ENODEV;
    }

    DBG_LEAVE( DbgInfo );
    return result;
} // wl_adapter_open
/*============================================================================*/

/*******************************************************************************
 *	wl_adapter_close()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Close the device
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure
 *
 *  RETURNS:
 *
 *      0
 *
 ******************************************************************************/
int wl_adapter_close( struct net_device *dev )
{
    DBG_FUNC( "wl_adapter_close" );
    DBG_ENTER( DbgInfo );

    DBG_TRACE( DbgInfo, "wl_adapter_close() -- PCI\n" );
    DBG_TRACE( DbgInfo, "%s: Shutting down adapter.\n", dev->name );

    wl_close( dev );

    DBG_LEAVE( DbgInfo );
    return 0;
} // wl_adapter_close
/*============================================================================*/

/*******************************************************************************
 *	wl_adapter_is_open()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Check whether this device is open. Returns
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the device's net_device structure
 *
 *  RETURNS:
 *
 *      nonzero if device is open.
 *
 ******************************************************************************/
int wl_adapter_is_open( struct net_device *dev )
{
    /* This function is used in PCMCIA to check the status of the 'open' field
       in the dev_link_t structure associated with a network device. There
       doesn't seem to be an analog to this for PCI, and checking the status
       contained in the net_device structure doesn't have the same effect.
       For now, return TRUE, but find out if this is necessary for PCI. */

    return TRUE;
} // wl_adapter_is_open
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_probe()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Registered in the pci_driver structure, this function is called when the
 *  PCI subsystem finds a new PCI device which matches the infomation contained
 *  in the pci_device_id table.
 *
 *  PARAMETERS:
 *
 *      pdev    - a pointer to the device's pci_dev structure
 *      ent     - this device's entry in the pci_device_id table
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int __devinit wl_pci_probe( struct pci_dev *pdev,
                                const struct pci_device_id *ent )
{
    int result;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_pci_probe" );
    DBG_ENTER( DbgInfo );
	DBG_PRINT( "%s\n", VERSION_INFO );

    result = wl_pci_setup( pdev );

    DBG_LEAVE( DbgInfo );

    return result;
} // wl_pci_probe
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_remove()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Registered in the pci_driver structure, this function is called when the
 *  PCI subsystem detects that a PCI device which matches the infomation
 *  contained in the pci_device_id table has been removed.
 *
 *  PARAMETERS:
 *
 *      pdev - a pointer to the device's pci_dev structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void __devexit wl_pci_remove(struct pci_dev *pdev)
{
    struct net_device       *dev = NULL;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_pci_remove" );
    DBG_ENTER( DbgInfo );

    /* Make sure the pci_dev pointer passed in is valid */
    if( pdev == NULL ) {
        DBG_ERROR( DbgInfo, "PCI subsys passed in an invalid pci_dev pointer\n" );
        return;
    }

    dev = (struct net_device *)pci_get_drvdata( pdev );
    if( dev == NULL ) {
        DBG_ERROR( DbgInfo, "Could not retrieve net_device structure\n" );
        return;
    }

    /* Perform device cleanup */
    wl_remove( dev );
    free_irq( dev->irq, dev );

#ifdef ENABLE_DMA
    wl_pci_dma_free( pdev, (struct wl_private *)dev->priv );
#endif

    wl_device_dealloc( dev );

    DBG_LEAVE( DbgInfo );
    return;
} // wl_pci_remove
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_setup()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Called by wl_pci_probe() to begin a device's initialization process.
 *
 *  PARAMETERS:
 *
 *      pdev - a pointer to the device's pci_dev structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_setup( struct pci_dev *pdev )
{
    int                 result = 0;
    struct net_device   *dev = NULL;
    struct wl_private   *lp = NULL;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_pci_setup" );
    DBG_ENTER( DbgInfo );

    /* Make sure the pci_dev pointer passed in is valid */
    if( pdev == NULL ) {
        DBG_ERROR( DbgInfo, "PCI subsys passed in an invalid pci_dev pointer\n" );
        return -ENODEV;
    }

    result = pci_enable_device( pdev );
    if( result != 0 ) {
        DBG_ERROR( DbgInfo, "pci_enable_device() failed\n" );
        DBG_LEAVE( DbgInfo );
        return result;
    }

    /* We found our device! Let's register it with the system */
    DBG_TRACE( DbgInfo, "Found our device, now registering\n" );
    dev = wl_device_alloc( );
    if( dev == NULL ) {
        DBG_ERROR( DbgInfo, "Could not register device!!!\n" );
        DBG_LEAVE( DbgInfo );
        return -ENOMEM;
    }

    /* Make sure that space was allocated for our private adapter struct */
    if( dev->priv == NULL ) {
        DBG_ERROR( DbgInfo, "Private adapter struct was not allocated!!!\n" );
        DBG_LEAVE( DbgInfo );
        return -ENOMEM;
    }

#ifdef ENABLE_DMA
    /* Allocate DMA Descriptors */
    if( wl_pci_dma_alloc( pdev, (struct wl_private *)dev->priv ) < 0 ) {
        DBG_ERROR( DbgInfo, "Could not allocate DMA descriptor memory!!!\n" );
        DBG_LEAVE( DbgInfo );
        return -ENOMEM;
    }
#endif

    /* Register our private adapter structure with PCI */
    pci_set_drvdata( pdev, dev );

    /* Fill out bus specific information in the net_device struct */
    dev->irq = pdev->irq;
    SET_MODULE_OWNER( dev );

    DBG_TRACE( DbgInfo, "Device Base Address: %#03lx\n", pdev->resource[0].start );
	dev->base_addr = pdev->resource[0].start;

    /* Initialize our device here */
    if( !wl_adapter_insert( dev )) {
        DBG_ERROR( DbgInfo, "wl_adapter_insert() FAILED!!!\n" );
        wl_device_dealloc( dev );
        DBG_LEAVE( DbgInfo );
        return -EINVAL;
    }

    /* Register our ISR */
    DBG_TRACE( DbgInfo, "Registering ISR...\n" );

    result = request_irq(dev->irq, wl_isr, SA_SHIRQ, dev->name, dev);
    if( result ) {
        DBG_WARNING( DbgInfo, "Could not register ISR!!!\n" );
        DBG_LEAVE( DbgInfo );
        return result;
	}

    /* Make sure interrupts are enabled properly for CardBus */
    lp = (struct wl_private *)dev->priv;

    if( lp->hcfCtx.IFB_BusType == CFG_NIC_BUS_TYPE_CARDBUS ||
	    lp->hcfCtx.IFB_BusType == CFG_NIC_BUS_TYPE_PCI 		) {
        DBG_TRACE( DbgInfo, "This is a PCI/CardBus card, enable interrupts\n" );
        wl_pci_enable_cardbus_interrupts( pdev );
    }

    /* Enable bus mastering */
    pci_set_master( pdev );

    DBG_LEAVE( DbgInfo );
    return 0;
} // wl_pci_setup
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_enable_cardbus_interrupts()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Called by wl_pci_setup() to enable interrupts on a CardBus device. This
 *  is done by writing bit 15 to the function event mask register. This
 *  CardBus-specific register is located in BAR2 (counting from BAR0), in memory
 *  space at byte offset 1f4 (7f4 for WARP).
 *
 *  PARAMETERS:
 *
 *      pdev - a pointer to the device's pci_dev structure
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_pci_enable_cardbus_interrupts( struct pci_dev *pdev )
{
    u32                 bar2_reg;
    u32                 mem_addr_bus;
    u32                 func_evt_mask_reg;
    void                *mem_addr_kern = NULL;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_pci_enable_cardbus_interrupts" );
    DBG_ENTER( DbgInfo );

    /* Initialize to known bad values */
    bar2_reg = 0xdeadbeef;
    mem_addr_bus = 0xdeadbeef;

    /* Read the BAR2 register; this register contains the base address of the
       memory region where the function event mask register lives */
    pci_read_config_dword( pdev, PCI_BASE_ADDRESS_2, &bar2_reg );
    mem_addr_bus = bar2_reg & PCI_BASE_ADDRESS_MEM_MASK;

    /* Once the base address is obtained, remap the memory region to kernel
       space so we can retrieve the register */
    mem_addr_kern = ioremap( mem_addr_bus, 0x200 );

#ifdef HERMES25
#define REG_OFFSET  0x07F4
#else
#define REG_OFFSET  0x01F4
#endif // HERMES25

#define BIT15       0x8000

    /* Retrieve the functional event mask register, enable interrupts by
       setting Bit 15, and write back the value */
    func_evt_mask_reg = *(u32 *)( mem_addr_kern + REG_OFFSET );
    func_evt_mask_reg |= BIT15;
    *(u32 *)( mem_addr_kern + REG_OFFSET ) = func_evt_mask_reg;

    /* Once complete, unmap the region and exit */
    iounmap( mem_addr_kern );

    DBG_LEAVE( DbgInfo );
    return;
} // wl_pci_enable_cardbus_interrupts
/*============================================================================*/

#ifdef ENABLE_DMA
/*******************************************************************************
 *	wl_pci_dma_alloc()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Allocates all resources needed for PCI/CardBus DMA operation
 *
 *  PARAMETERS:
 *
 *      pdev - a pointer to the device's pci_dev structure
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_alloc( struct pci_dev *pdev, struct wl_private *lp )
{
    int i;
    int status = 0;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_pci_dma_alloc" );
    DBG_ENTER( DbgInfo );

//     lp->dma.tx_rsc_ind = lp->dma.rx_rsc_ind = 0;
//
//     /* Alloc for the Tx chain and its reclaim descriptor */
//     for( i = 0; i < NUM_TX_DESC; i++ ) {
//         status = wl_pci_dma_alloc_tx_packet( pdev, lp, &lp->dma.tx_packet[i] );
//         if( status == 0 ) {
//             DBG_PRINT( "lp->dma.tx_packet[%d] :                 0x%p\n", i, lp->dma.tx_packet[i] );
//             DBG_PRINT( "lp->dma.tx_packet[%d]->next_desc_addr : 0x%p\n", i, lp->dma.tx_packet[i]->next_desc_addr );
//             lp->dma.tx_rsc_ind++;
//         } else {
//             DBG_ERROR( DbgInfo, "Could not alloc DMA Tx Packet\n" );
//             break;
//         }
//     }
//     if( status == 0 ) {
//         status = wl_pci_dma_alloc_desc( pdev, lp, &lp->dma.tx_reclaim_desc );
//         DBG_PRINT( "lp->dma.tx_reclaim_desc: 0x%p\n", lp->dma.tx_reclaim_desc );
//     }
//     /* Alloc for the Rx chain and its reclaim descriptor */
//     if( status == 0 ) {
//         for( i = 0; i < NUM_RX_DESC; i++ ) {
//             status = wl_pci_dma_alloc_rx_packet( pdev, lp, &lp->dma.rx_packet[i] );
//             if( status == 0 ) {
//                 DBG_PRINT( "lp->dma.rx_packet[%d]                 : 0x%p\n", i, lp->dma.rx_packet[i] );
//                 DBG_PRINT( "lp->dma.rx_packet[%d]->next_desc_addr : 0x%p\n", i, lp->dma.rx_packet[i]->next_desc_addr );
//                 lp->dma.rx_rsc_ind++;
//             } else {
//                 DBG_ERROR( DbgInfo, "Could not alloc DMA Rx Packet\n" );
//                 break;
//             }
//         }
//     }
//     if( status == 0 ) {
//         status = wl_pci_dma_alloc_desc( pdev, lp, &lp->dma.rx_reclaim_desc );
//         DBG_PRINT( "lp->dma.rx_reclaim_desc: 0x%p\n", lp->dma.rx_reclaim_desc );
//     }
//     /* Store status, as host should not call HCF functions if this fails */
//     lp->dma.status = status;  //;?all useages of dma.status have been commented out
//     DBG_LEAVE( DbgInfo );
    return status;
} // wl_pci_dma_alloc
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_free()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Deallocated all resources needed for PCI/CardBus DMA operation
 *
 *  PARAMETERS:
 *
 *      pdev - a pointer to the device's pci_dev structure
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_free( struct pci_dev *pdev, struct wl_private *lp )
{
    int i;
    int status = 0;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_pci_dma_free" );
    DBG_ENTER( DbgInfo );

    /* Reclaim all Rx packets that were handed over to the HCF */
    /* Do I need to do this? Before this free is called, I've already disabled
       the port which will call wl_pci_dma_hcf_reclaim */
    //if( lp->dma.status == 0 )
    //{
    //    wl_pci_dma_hcf_reclaim( lp );
    //}

    /* Free everything needed for DMA Rx */
    for( i = 0; i < NUM_RX_DESC; i++ ) {
        if( lp->dma.rx_packet[i] ) {
            status = wl_pci_dma_free_rx_packet( pdev, lp, &lp->dma.rx_packet[i] );
            if( status != 0 ) {
                DBG_WARNING( DbgInfo, "Problem freeing Rx packet\n" );
            }
        }
    }
    lp->dma.rx_rsc_ind = 0;

    if( lp->dma.rx_reclaim_desc ) {
        status = wl_pci_dma_free_desc( pdev, lp, &lp->dma.rx_reclaim_desc );
        if( status != 0 ) {
            DBG_WARNING( DbgInfo, "Problem freeing Rx reclaim descriptor\n" );
        }
    }

    /* Free everything needed for DMA Tx */
    for( i = 0; i < NUM_TX_DESC; i++ ) {
        if( lp->dma.tx_packet[i] ) {
            status = wl_pci_dma_free_tx_packet( pdev, lp, &lp->dma.tx_packet[i] );
            if( status != 0 ) {
                DBG_WARNING( DbgInfo, "Problem freeing Tx packet\n" );
            }
        }
    }
    lp->dma.tx_rsc_ind = 0;

    if( lp->dma.tx_reclaim_desc ) {
        status = wl_pci_dma_free_desc( pdev, lp, &lp->dma.tx_reclaim_desc );
        if( status != 0 ) {
            DBG_WARNING( DbgInfo, "Problem freeing Tx reclaim descriptor\n" );
        }
    }

    DBG_LEAVE( DbgInfo );
    return status;
} // wl_pci_dma_free

/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_alloc_tx_packet()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Allocates a single Tx packet, consisting of several descriptors and
 *      buffers. Data to transmit is first copied into the 'payload' buffer
 *      before being transmitted.
 *
 *  PARAMETERS:
 *
 *      pdev    - a pointer to the device's pci_dev structure
 *      lp      - the device's private adapter structure
 *      desc    - a pointer which will reference the descriptor to be alloc'd.
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_alloc_tx_packet( struct pci_dev *pdev, struct wl_private *lp,
                                DESC_STRCT **desc )
{
//     int status = 0;
//     /*------------------------------------------------------------------------*/
//
//     if( desc == NULL ) {
//         status = -EFAULT;
//     }
//     if( status == 0 ) {
//         status = wl_pci_dma_alloc_desc_and_buf( pdev, lp, desc,
//                                                 HCF_DMA_TX_BUF1_SIZE );
//
//         if( status == 0 ) {
//             status = wl_pci_dma_alloc_desc_and_buf( pdev, lp,
//                                                     &( (*desc)->next_desc_addr ),
//                                                     HCF_MAX_PACKET_SIZE );
//         }
//     }
//     if( status == 0 ) {
//         (*desc)->next_desc_phys_addr = (*desc)->next_desc_addr->desc_phys_addr;
//     }
//     return status;
} // wl_pci_dma_alloc_tx_packet
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_free_tx_packet()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Frees a single Tx packet, described in the corresponding alloc function.
 *
 *  PARAMETERS:
 *
 *      pdev    - a pointer to the device's pci_dev structure
 *      lp      - the device's private adapter structure
 *      desc    - a pointer which will reference the descriptor to be alloc'd.
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_free_tx_packet( struct pci_dev *pdev, struct wl_private *lp,
                                DESC_STRCT **desc )
{
    int status = 0;
    /*------------------------------------------------------------------------*/

    if( *desc == NULL ) {
        DBG_PRINT( "Null descriptor\n" );
        status = -EFAULT;
    }
	//;?the "limited" NDIS strategy, assuming a frame consists ALWAYS out of 2
	//descriptors, make this robust
    if( status == 0 && (*desc)->next_desc_addr ) {
        status = wl_pci_dma_free_desc_and_buf( pdev, lp, &(*desc)->next_desc_addr );
    }
    if( status == 0 ) {
        status = wl_pci_dma_free_desc_and_buf( pdev, lp, desc );
    }
    return status;
} // wl_pci_dma_free_tx_packet
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_alloc_rx_packet()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Allocates a single Rx packet, consisting of two descriptors and one
 *      contiguous buffer. THe buffer starts with the hermes-specific header.
 *      One descriptor points at the start, the other at offset 0x3a of the
 *      buffer.
 *
 *  PARAMETERS:
 *
 *      pdev    - a pointer to the device's pci_dev structure
 *      lp      - the device's private adapter structure
 *      desc    - a pointer which will reference the descriptor to be alloc'd.
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_alloc_rx_packet( struct pci_dev *pdev, struct wl_private *lp,
                                DESC_STRCT **desc )
{
    int         status = 0;
    DESC_STRCT  *p;
    /*------------------------------------------------------------------------*/

//     if( desc == NULL ) {
//         status = -EFAULT;
//     }
// 	//;?the "limited" NDIS strategy, assuming a frame consists ALWAYS out of 2
// 	//descriptors, make this robust
//     if( status == 0 ) {
//         status = wl_pci_dma_alloc_desc( pdev, lp, desc );
// 	}
//     if( status == 0 ) {
//         status = wl_pci_dma_alloc_buf( pdev, lp, *desc, HCF_MAX_PACKET_SIZE );
//     }
//     if( status == 0 ) {
//         status = wl_pci_dma_alloc_desc( pdev, lp, &p );
//     }
//     if( status == 0 ) {
//         /* Size of 1st descriptor becomes 0x3a bytes */
//         SET_BUF_SIZE( *desc, HCF_DMA_RX_BUF1_SIZE );
//
//         /* Make 2nd descriptor point at offset 0x3a of the buffer */
//         SET_BUF_SIZE( p, ( HCF_MAX_PACKET_SIZE - HCF_DMA_RX_BUF1_SIZE ));
//         p->buf_addr       = (*desc)->buf_addr + HCF_DMA_RX_BUF1_SIZE;
//         p->buf_phys_addr  = (*desc)->buf_phys_addr + HCF_DMA_RX_BUF1_SIZE;
//         p->next_desc_addr = NULL;
//
//         /* Chain 2nd descriptor to 1st descriptor */
//         (*desc)->next_desc_addr      = p;
//         (*desc)->next_desc_phys_addr = p->desc_phys_addr;
//     }

    return status;
} // wl_pci_dma_alloc_rx_packet
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_free_rx_packet()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Frees a single Rx packet, described in the corresponding alloc function.
 *
 *  PARAMETERS:
 *
 *      pdev    - a pointer to the device's pci_dev structure
 *      lp      - the device's private adapter structure
 *      desc    - a pointer which will reference the descriptor to be alloc'd.
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_free_rx_packet( struct pci_dev *pdev, struct wl_private *lp,
                                DESC_STRCT **desc )
{
    int status = 0;
    DESC_STRCT *p;
    /*------------------------------------------------------------------------*/

    if( *desc == NULL ) {
        status = -EFAULT;
    }
    if( status == 0 ) {
        p = (*desc)->next_desc_addr;

        /* Free the 2nd descriptor */
        if( p != NULL ) {
            p->buf_addr      = NULL;
            p->buf_phys_addr = 0;

            status = wl_pci_dma_free_desc( pdev, lp, &p );
        }
    }

    /* Free the buffer and 1st descriptor */
    if( status == 0 ) {
        SET_BUF_SIZE( *desc, HCF_MAX_PACKET_SIZE );
        status = wl_pci_dma_free_desc_and_buf( pdev, lp, desc );
    }
    return status;
} // wl_pci_dma_free_rx_packet
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_alloc_desc_and_buf()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Allocates a DMA descriptor and buffer, and associates them with one
 *      another.
 *
 *  PARAMETERS:
 *
 *      pdev  - a pointer to the device's pci_dev structure
 *      lp    - the device's private adapter structure
 *      desc  - a pointer which will reference the descriptor to be alloc'd
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_alloc_desc_and_buf( struct pci_dev *pdev, struct wl_private *lp,
                                   DESC_STRCT **desc, int size )
{
    int status = 0;
    /*------------------------------------------------------------------------*/

//     if( desc == NULL ) {
//         status = -EFAULT;
//     }
//     if( status == 0 ) {
//         status = wl_pci_dma_alloc_desc( pdev, lp, desc );
//
//         if( status == 0 ) {
//             status = wl_pci_dma_alloc_buf( pdev, lp, *desc, size );
//         }
//     }
    return status;
} // wl_pci_dma_alloc_desc_and_buf
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_free_desc_and_buf()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Frees a DMA descriptor and associated buffer.
 *
 *  PARAMETERS:
 *
 *      pdev  - a pointer to the device's pci_dev structure
 *      lp    - the device's private adapter structure
 *      desc  - a pointer which will reference the descriptor to be alloc'd
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_free_desc_and_buf( struct pci_dev *pdev, struct wl_private *lp,
                                   DESC_STRCT **desc )
{
    int status = 0;
    /*------------------------------------------------------------------------*/

    if( desc == NULL ) {
        status = -EFAULT;
    }
    if( status == 0 && *desc == NULL ) {
        status = -EFAULT;
    }
    if( status == 0 ) {
        status = wl_pci_dma_free_buf( pdev, lp, *desc );

        if( status == 0 ) {
            status = wl_pci_dma_free_desc( pdev, lp, desc );
        }
    }
    return status;
} // wl_pci_dma_free_desc_and_buf
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_alloc_desc()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Allocates one DMA descriptor in cache coherent memory.
 *
 *  PARAMETERS:
 *
 *      pdev - a pointer to the device's pci_dev structure
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_alloc_desc( struct pci_dev *pdev, struct wl_private *lp,
                           DESC_STRCT **desc )
{
//     int         status = 0;
//     dma_addr_t  pa;
//     /*------------------------------------------------------------------------*/
//
//     DBG_FUNC( "wl_pci_dma_alloc_desc" );
//     DBG_ENTER( DbgInfo );
//
//     if( desc == NULL ) {
//         status = -EFAULT;
//     }
//     if( status == 0 ) {
//         *desc = pci_alloc_consistent( pdev, sizeof( DESC_STRCT ), &pa );
//     }
//     if( *desc == NULL ) {
//         DBG_ERROR( DbgInfo, "pci_alloc_consistent() failed\n" );
//         status = -ENOMEM;
//     } else {
//         memset( *desc, 0, sizeof( DESC_STRCT ));
//         (*desc)->desc_phys_addr = cpu_to_le32( pa );
//     }
//     DBG_LEAVE( DbgInfo );
//     return status;
} // wl_pci_dma_alloc_desc
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_free_desc()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Frees one DMA descriptor in cache coherent memory.
 *
 *  PARAMETERS:
 *
 *      pdev - a pointer to the device's pci_dev structure
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_free_desc( struct pci_dev *pdev, struct wl_private *lp,
                           DESC_STRCT **desc )
{
    int         status = 0;
    /*------------------------------------------------------------------------*/

    if( *desc == NULL ) {
        status = -EFAULT;
    }
    if( status == 0 ) {
        pci_free_consistent( pdev, sizeof( DESC_STRCT ), *desc,
                             (*desc)->desc_phys_addr );
    }
    *desc = NULL;
    return status;
} // wl_pci_dma_free_desc
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_alloc_buf()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Allocates one DMA buffer in cache coherent memory, and associates a DMA
 *      descriptor with this buffer.
 *
 *  PARAMETERS:
 *
 *      pdev - a pointer to the device's pci_dev structure
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_alloc_buf( struct pci_dev *pdev, struct wl_private *lp,
                          DESC_STRCT *desc, int size )
{
    int         status = 0;
    dma_addr_t  pa;
    /*------------------------------------------------------------------------*/

//     DBG_FUNC( "wl_pci_dma_alloc_buf" );
//     DBG_ENTER( DbgInfo );
//
//     if( desc == NULL ) {
//         status = -EFAULT;
//     }
//     if( status == 0 && desc->buf_addr != NULL ) {
//         status = -EFAULT;
//     }
//     if( status == 0 ) {
//         desc->buf_addr = pci_alloc_consistent( pdev, size, &pa );
//     }
//     if( desc->buf_addr == NULL ) {
//         DBG_ERROR( DbgInfo, "pci_alloc_consistent() failed\n" );
//         status = -ENOMEM;
//     } else {
//         desc->buf_phys_addr = cpu_to_le32( pa );
//         SET_BUF_SIZE( desc, size );
//     }
//     DBG_LEAVE( DbgInfo );
    return status;
} // wl_pci_dma_alloc_buf
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_free_buf()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Allocates one DMA buffer in cache coherent memory, and associates a DMA
 *      descriptor with this buffer.
 *
 *  PARAMETERS:
 *
 *      pdev - a pointer to the device's pci_dev structure
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_pci_dma_free_buf( struct pci_dev *pdev, struct wl_private *lp,
                         DESC_STRCT *desc )
{
    int         status = 0;
    /*------------------------------------------------------------------------*/

    if( desc == NULL ) {
        status = -EFAULT;
    }
    if( status == 0 && desc->buf_addr == NULL ) {
        status = -EFAULT;
    }
    if( status == 0 ) {
        pci_free_consistent( pdev, GET_BUF_SIZE( desc ), desc->buf_addr,
                             desc->buf_phys_addr );

        desc->buf_addr = 0;
        desc->buf_phys_addr = 0;
        SET_BUF_SIZE( desc, 0 );
    }
    return status;
} // wl_pci_dma_free_buf
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_hcf_supply()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Supply HCF with DMA-related resources. These consist of:
 *          - buffers and descriptors for receive purposes
 *          - one 'reclaim' descriptor for the transmit path, used to fulfill a
 *            certain H25 DMA engine requirement
 *          - one 'reclaim' descriptor for the receive path, used to fulfill a
 *            certain H25 DMA engine requirement
 *
 *      This function is called at start-of-day or at re-initialization.
 *
 *  PARAMETERS:
 *
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
void wl_pci_dma_hcf_supply( struct wl_private *lp )
{
    int i;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_pci_dma_hcf_supply" );
    DBG_ENTER( DbgInfo );

    //if( lp->dma.status == 0 );
    //{
        /* Hand over the Rx/Tx reclaim descriptors to the HCF */
        if( lp->dma.tx_reclaim_desc ) {
            DBG_PRINT( "lp->dma.tx_reclaim_desc: 0x%p\n", lp->dma.tx_reclaim_desc );
            hcf_dma_tx_put( &lp->hcfCtx, lp->dma.tx_reclaim_desc, 0 );
            lp->dma.tx_reclaim_desc = NULL;
            DBG_PRINT( "lp->dma.tx_reclaim_desc: 0x%p\n", lp->dma.tx_reclaim_desc );
        }
        if( lp->dma.rx_reclaim_desc ) {
            DBG_PRINT( "lp->dma.rx_reclaim_desc: 0x%p\n", lp->dma.rx_reclaim_desc );
            hcf_dma_rx_put( &lp->hcfCtx, lp->dma.rx_reclaim_desc );
            lp->dma.rx_reclaim_desc = NULL;
            DBG_PRINT( "lp->dma.rx_reclaim_desc: 0x%p\n", lp->dma.rx_reclaim_desc );
        }
        /* Hand over the Rx descriptor chain to the HCF */
        for( i = 0; i < NUM_RX_DESC; i++ ) {
            DBG_PRINT( "lp->dma.rx_packet[%d]:    0x%p\n", i, lp->dma.rx_packet[i] );
            hcf_dma_rx_put( &lp->hcfCtx, lp->dma.rx_packet[i] );
            lp->dma.rx_packet[i] = NULL;
            DBG_PRINT( "lp->dma.rx_packet[%d]:    0x%p\n", i, lp->dma.rx_packet[i] );
        }
    //}

    DBG_LEAVE( DbgInfo );
    return;
} // wl_pci_dma_hcf_supply
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_hcf_reclaim()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Return DMA-related resources from the HCF. These consist of:
 *          - buffers and descriptors for receive purposes
 *          - buffers and descriptors for transmit purposes
 *          - one 'reclaim' descriptor for the transmit path, used to fulfill a
 *            certain H25 DMA engine requirement
 *          - one 'reclaim' descriptor for the receive path, used to fulfill a
 *            certain H25 DMA engine requirement
 *
 *      This function is called at end-of-day or at re-initialization.
 *
 *  PARAMETERS:
 *
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
void wl_pci_dma_hcf_reclaim( struct wl_private *lp )
{
    int i;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_pci_dma_hcf_reclaim" );
    DBG_ENTER( DbgInfo );

    wl_pci_dma_hcf_reclaim_rx( lp );
    for( i = 0; i < NUM_RX_DESC; i++ ) {
        DBG_PRINT( "rx_packet[%d] 0x%p\n", i, lp->dma.rx_packet[i] );
//         if( lp->dma.rx_packet[i] == NULL ) {
//             DBG_PRINT( "wl_pci_dma_hcf_reclaim: rx_packet[%d] NULL\n", i );
//         }
    }

    wl_pci_dma_hcf_reclaim_tx( lp );
    for( i = 0; i < NUM_TX_DESC; i++ ) {
        DBG_PRINT( "tx_packet[%d] 0x%p\n", i, lp->dma.tx_packet[i] );
//         if( lp->dma.tx_packet[i] == NULL ) {
//             DBG_PRINT( "wl_pci_dma_hcf_reclaim: tx_packet[%d] NULL\n", i );
//         }
     }

    DBG_LEAVE( DbgInfo );
    return;
} // wl_pci_dma_hcf_reclaim
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_hcf_reclaim_rx()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Reclaim Rx packets that have already been processed by the HCF.
 *
 *  PARAMETERS:
 *
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
void wl_pci_dma_hcf_reclaim_rx( struct wl_private *lp )
{
    int         i;
    DESC_STRCT *p;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_pci_dma_hcf_reclaim_rx" );
    DBG_ENTER( DbgInfo );

    //if( lp->dma.status == 0 )
    //{
        while ( ( p = hcf_dma_rx_get( &lp->hcfCtx ) ) != NULL ) {
            if( p && p->buf_addr == NULL ) {
                /* A reclaim descriptor is being given back by the HCF. Reclaim
                   descriptors have a NULL buf_addr */
                lp->dma.rx_reclaim_desc = p;
            	DBG_PRINT( "reclaim_descriptor: 0x%p\n", p );
                continue;
            }
            for( i = 0; i < NUM_RX_DESC; i++ ) {
                if( lp->dma.rx_packet[i] == NULL ) {
                    break;
                }
            }
            /* An Rx buffer descriptor is being given back by the HCF */
            lp->dma.rx_packet[i] = p;
            lp->dma.rx_rsc_ind++;
        	DBG_PRINT( "rx_packet[%d] 0x%p\n", i, lp->dma.rx_packet[i] );
        }
    //}
    DBG_LEAVE( DbgInfo );
} // wl_pci_dma_hcf_reclaim_rx
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_get_tx_packet()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Obtains a Tx descriptor from the chain to use for Tx.
 *
 *  PARAMETERS:
 *
 *      lp - a pointer to the device's wl_private structure.
 *
 *  RETURNS:
 *
 *      A pointer to the retrieved descriptor
 *
 ******************************************************************************/
DESC_STRCT * wl_pci_dma_get_tx_packet( struct wl_private *lp )
{
    int i;
    DESC_STRCT *desc = NULL;
    /*------------------------------------------------------------------------*/

    for( i = 0; i < NUM_TX_DESC; i++ ) {
        if( lp->dma.tx_packet[i] ) {
            break;
        }
    }

    if( i != NUM_TX_DESC ) {
        desc = lp->dma.tx_packet[i];

        lp->dma.tx_packet[i] = NULL;
        lp->dma.tx_rsc_ind--;

        memset( desc->buf_addr, 0, HCF_DMA_TX_BUF1_SIZE );
    }

    return desc;
} // wl_pci_dma_get_tx_packet
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_put_tx_packet()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Returns a Tx descriptor to the chain.
 *
 *  PARAMETERS:
 *
 *      lp   - a pointer to the device's wl_private structure.
 *      desc - a pointer to the descriptor to return.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_pci_dma_put_tx_packet( struct wl_private *lp, DESC_STRCT *desc )
{
    int i;
    /*------------------------------------------------------------------------*/

    for( i = 0; i < NUM_TX_DESC; i++ ) {
        if( lp->dma.tx_packet[i] == NULL ) {
            break;
        }
    }

    if( i != NUM_TX_DESC ) {
        lp->dma.tx_packet[i] = desc;
        lp->dma.tx_rsc_ind++;
    }
} // wl_pci_dma_put_tx_packet
/*============================================================================*/

/*******************************************************************************
 *	wl_pci_dma_hcf_reclaim_tx()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Reclaim Tx packets that have either been processed by the HCF due to a
 *      port disable or a Tx completion.
 *
 *  PARAMETERS:
 *
 *      lp  - the device's private adapter structure
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
void wl_pci_dma_hcf_reclaim_tx( struct wl_private *lp )
{
    int         i;
    DESC_STRCT *p;
    /*------------------------------------------------------------------------*/

    DBG_FUNC( "wl_pci_dma_hcf_reclaim_tx" );
    DBG_ENTER( DbgInfo );

    //if( lp->dma.status == 0 )
    //{
        while ( ( p = hcf_dma_tx_get( &lp->hcfCtx ) ) != NULL ) {

            if( p != NULL && p->buf_addr == NULL ) {
                /* A Reclaim descriptor is being given back by the HCF. Reclaim
                   descriptors have a NULL buf_addr */
                lp->dma.tx_reclaim_desc = p;
            	DBG_PRINT( "reclaim_descriptor: 0x%p\n", p );
                continue;
            }
            for( i = 0; i < NUM_TX_DESC; i++ ) {
                if( lp->dma.tx_packet[i] == NULL ) {
                    break;
                }
            }
            /* An Rx buffer descriptor is being given back by the HCF */
            lp->dma.tx_packet[i] = p;
            lp->dma.tx_rsc_ind++;
        	DBG_PRINT( "tx_packet[%d] 0x%p\n", i, lp->dma.tx_packet[i] );
        }
    //}

    if( lp->netif_queue_on == FALSE ) {
        netif_wake_queue( lp->dev );
        WL_WDS_NETIF_WAKE_QUEUE( lp );
        lp->netif_queue_on = TRUE;
    }
    DBG_LEAVE( DbgInfo );
    return;
} // wl_pci_dma_hcf_reclaim_tx
/*============================================================================*/
#endif  // ENABLE_DMA
