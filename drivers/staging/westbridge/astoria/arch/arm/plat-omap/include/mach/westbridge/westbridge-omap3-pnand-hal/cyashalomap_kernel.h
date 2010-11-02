/* Cypress Antioch HAL for OMAP KERNEL header file (cyashalomapkernel.h)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/

/*
 * This file contains the defintion of the hardware abstraction
 * layer on OMAP3430 talking to the West Bridge Astoria device
 */


#ifndef _INCLUDED_CYASHALOMAP_KERNEL_H_
#define _INCLUDED_CYASHALOMAP_KERNEL_H_

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/string.h>
/* include does not seem to work
 * moving for patch submission
#include <mach/gpmc.h>
*/
#include <linux/../../arch/arm/plat-omap/include/plat/gpmc.h>
typedef struct cy_as_hal_sleep_channel_t {
	wait_queue_head_t wq;
} cy_as_hal_sleep_channel;

/* moved to staging location, eventual location
 * considered is here
#include <mach/westbridge/cyashaldef.h>
#include <linux/westbridge/cyastypes.h>
#include <linux/westbridge/cyas_cplus_start.h>
*/
#include "../cyashaldef.h"
#include "../../../../../../../include/linux/westbridge/cyastypes.h"
#include "../../../../../../../include/linux/westbridge/cyas_cplus_start.h"
#include "cyasomapdev_kernel.h"

/*
 * Below are the data structures that must be defined by the HAL layer
 */

/*
 * The HAL layer must define a TAG for identifying a specific Astoria
 * device in the system. In this case the tag is a void * which is
 * really an OMAP device pointer
 */
typedef void *cy_as_hal_device_tag;


/* This must be included after the CyAsHalDeviceTag type is defined */

/* moved to staging location, eventual location
 * considered is here
 * #include <linux/westbridge/cyashalcb.h>
*/
#include "../../../../../../../include/linux/westbridge/cyashalcb.h"
/*
 * Below are the functions that communicate with the West Bridge
 * device.  These are system dependent and must be defined by
 * the HAL layer for a given system.
 */

/*
 * This function must be defined to write a register within the Antioch
 * device.  The addr value is the address of the register to write with
 * respect to the base address of the Antioch device.
 */
void
cy_as_hal_write_register(cy_as_hal_device_tag tag,
	uint16_t addr, uint16_t data);

/*
 * This function must be defined to read a register from
 * the west bridge device.  The addr value is the address of
 * the register to read with respect to the base address
 * of the west bridge device.
 */
uint16_t
cy_as_hal_read_register(cy_as_hal_device_tag tag, uint16_t addr);

/*
 * This function must be defined to transfer a block of data
 * to the west bridge device.  This function can use the burst write
 * (DMA) capabilities of Antioch to do this, or it can just copy
 * the data using writes.
 */
void
cy_as_hal_dma_setup_write(cy_as_hal_device_tag tag,
	uint8_t ep, void *buf, uint32_t size, uint16_t maxsize);

/*
 * This function must be defined to transfer a block of data
 * from the Antioch device.  This function can use the burst
 * read (DMA) capabilities of Antioch to do this, or it can
 * just copy the data using reads.
 */
void
cy_as_hal_dma_setup_read(cy_as_hal_device_tag tag, uint8_t ep,
	void *buf, uint32_t size, uint16_t maxsize);

/*
 * This function must be defined to cancel any pending DMA request.
 */
void
cy_as_hal_dma_cancel_request(cy_as_hal_device_tag tag, uint8_t ep);

/*
 * This function must be defined to allow the Antioch API to
 * register a callback function that is called when a DMA transfer
 * is complete.
 */
void
cy_as_hal_dma_register_callback(cy_as_hal_device_tag tag,
	cy_as_hal_dma_complete_callback cb);

/*
 * This function must be defined to return the maximum size of DMA
 * request that can be handled on the given endpoint.  The return
 * value should be the maximum size in bytes that the DMA module can
 * handle.
 */
uint32_t
cy_as_hal_dma_max_request_size(cy_as_hal_device_tag tag,
	cy_as_end_point_number_t ep);

/*
 * This function must be defined to set the state of the WAKEUP pin
 * on the Antioch device.  Generally this is done via a GPIO of some
 * type.
 */
cy_bool
cy_as_hal_set_wakeup_pin(cy_as_hal_device_tag tag, cy_bool state);

/*
 * This function is called when the Antioch PLL loses lock, because
 * of a problem in the supply voltage or the input clock.
 */
void
cy_as_hal_pll_lock_loss_handler(cy_as_hal_device_tag tag);


/**********************************************************************
 *
 * Below are the functions that must be defined to provide the basic
 * operating system services required by the API.
 *
***********************************************************************/

/*
 * This function is required by the API to allocate memory.  This function
 * is expected to work exactly like malloc().
 */
void *
cy_as_hal_alloc(uint32_t cnt);

/*
 * This function is required by the API to free memory allocated with
 * CyAsHalAlloc().  This function is expected to work exacly like free().
 */
void
cy_as_hal_free(void *mem_p);

/*
 * This function is required by the API to allocate memory during a
 * callback.  This function must be able to provide storage at inturupt
 * time.
 */
void *
cy_as_hal_c_b_alloc(uint32_t cnt);

/*
 * This function is required by the API to free memory allocated with
 * CyAsCBHalAlloc().
 */
void
cy_as_hal_c_b_free(void *ptr);

/*
 * This function is required to set a block of memory to a specific
 * value.  This function is expected to work exactly like memset()
 */
void
cy_as_hal_mem_set(void *ptr, uint8_t value, uint32_t cnt);

/*
 * This function is expected to create a sleep channel.  The data
 * structure that represents the sleep channel is given by the
 * pointer in the argument.
 */
cy_bool
cy_as_hal_create_sleep_channel(cy_as_hal_sleep_channel *channel);

/*
 * This function is expected to destroy a sleep channel.  The data
 * structure that represents the sleep channel is given by
 * the pointer in the argument.
 */


cy_bool
cy_as_hal_destroy_sleep_channel(cy_as_hal_sleep_channel *channel);

cy_bool
cy_as_hal_sleep_on(cy_as_hal_sleep_channel *channel, uint32_t ms);

cy_bool
cy_as_hal_wake(cy_as_hal_sleep_channel *channel);

uint32_t
cy_as_hal_disable_interrupts(void);

void
cy_as_hal_enable_interrupts(uint32_t);

void
cy_as_hal_sleep150(void);

void
cy_as_hal_sleep(uint32_t ms);

cy_bool
cy_as_hal_is_polling(void);

void cy_as_hal_init_dev_registers(cy_as_hal_device_tag tag,
	cy_bool is_standby_wakeup);

/*
 * required only in spi mode
 */
cy_bool cy_as_hal_sync_device_clocks(cy_as_hal_device_tag tag);

void cy_as_hal_read_regs_before_standby(cy_as_hal_device_tag tag);


#ifndef NDEBUG
#define cy_as_hal_assert(cond) if (!(cond))\
	printk(KERN_WARNING"assertion failed at %s:%d\n", __FILE__, __LINE__);
#else
#define cy_as_hal_assert(cond)
#endif

#define cy_as_hal_print_message printk

/* removable debug printks */
#ifndef WESTBRIDGE_NDEBUG
#define DBG_PRINT_ENABLED
#endif

/*#define MBOX_ACCESS_DBG_PRINT_ENABLED*/


#ifdef DBG_PRINT_ENABLED
 /* Debug printing enabled */

 #define DBGPRN(...) printk(__VA_ARGS__)
 #define DBGPRN_FUNC_NAME	printk("<1> %x:_func: %s\n", \
		current->pid, __func__)

#else
 /** NO DEBUG PRINTING **/
 #define DBGPRN(...)
 #define DBGPRN_FUNC_NAME

#endif

/*
CyAsMiscSetLogLevel(uint8_t level)
{
	debug_level = level;
}

#ifdef CY_AS_LOG_SUPPORT

void
cy_as_log_debug_message(int level, const char *str)
{
	if (level <= debug_level)
		cy_as_hal_print_message("log %d: %s\n", level, str);
}
*/


/*
 * print buffer helper
 */
void cyashal_prn_buf(void  *buf, uint16_t offset, int len);

/*
 * These are the functions that are not part of the HAL layer,
 * but are required to be called for this HAL.
 */
int start_o_m_a_p_kernel(const char *pgm,
	cy_as_hal_device_tag *tag, cy_bool debug);
int stop_o_m_a_p_kernel(const char *pgm, cy_as_hal_device_tag tag);
int omap_start_intr(cy_as_hal_device_tag tag);
void cy_as_hal_set_ep_dma_mode(uint8_t ep, bool sg_xfer_enabled);

/* moved to staging location
#include <linux/westbridge/cyas_cplus_end.h>
*/
#include "../../../../../../../include/linux/westbridge/cyas_cplus_start.h"
#endif
