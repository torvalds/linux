/* ==========================================================================
 * $File: //dwh/usb_iip/dev/software/otg_ipmate/linux/platform/dwc_otg_plat.h $
 * $Revision: #1 $
 * $Date: 2005/07/07 $
 * $Change: 510301 $
 *
 * Synopsys HS OTG Linux Software Driver and documentation (hereinafter,
 * "Software") is an Unsupported proprietary work of Synopsys, Inc. unless
 * otherwise expressly agreed to in writing between Synopsys and you.
 * 
 * The Software IS NOT an item of Licensed Software or Licensed Product under
 * any End User Software License Agreement or Agreement for Licensed Product
 * with Synopsys or any supplement thereto. You are permitted to use and
 * redistribute this Software in source and binary forms, with or without
 * modification, provided that redistributions of source code must retain this
 * notice. You may not view, use, disclose, copy or distribute this file or
 * any information contained herein except pursuant to this license grant from
 * Synopsys. If you do not agree with this notice, including the disclaimer
 * below, then you are not authorized to use the Software.
 * 
 * THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS" BASIS
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * ========================================================================== */

#if !defined(__DWC_OTG_PLAT_H__)
#define __DWC_OTG_PLAT_H__

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <asm/io.h>
#ifdef CONFIG_ARCH_RK29
#include <mach/rk29_iomap.h>
#include <mach/iomux.h>
#define GRF_REG_BASE	RK29_GRF_BASE	
#define USB20_OTG0_BASE	RK29_USBOTG0_PHYS
#define USB20_OTG1_BASE	RK29_USBOTG1_PHYS
#define USB11_HOST_BASE	RK29_USBHOST_PHYS
#define USBOTG_SIZE	RK29_USBOTG0_SIZE
#define USB_GRF_CON	(GRF_REG_BASE+0x9C)
#define USB_GRF_IOMUX	(GRF_REG_BASE+0x68)
#define USB_CLKGATE_CON	(RK29_CRU_BASE+0x60)
#define USB_CLKSEL_CON	(RK29_CRU_BASE+0x18)
#ifndef SCU_BASE_ADDR_VA
#define SCU_BASE_ADDR_VA RK29_CRU_BASE
#endif
#define USB_IOMUX_INIT(a,b) rk29_mux_api_set(a,b)
#endif

#ifdef CONFIG_ARCH_RK30
#include <mach/iomux.h>
#define GRF_REG_BASE	RK30_GRF_BASE	
#define USBOTG_SIZE	RK30_USBOTG20_SIZE
#define USBGRF_UOC0_CON2	(GRF_REG_BASE+0x184)
#define USBGRF_UOC1_CON2	(GRF_REG_BASE+0x190)
#define USB_IOMUX_INIT(a,b) rk30_mux_api_set(a,b)
#endif

/**
 * @file 
 *
 * This file contains the Platform Specific constants, interfaces
 * (functions and macros) for Linux.
 *
 */
#if !defined(__LINUX_ARM_ARCH__)
#error "The contents of this file is Linux specific!!!"
#endif

/**
 * Reads the content of a register.
 *
 * @param _reg address of register to read.
 * @return contents of the register.
 *

 * Usage:<br>
 * <code>uint32_t dev_ctl = dwc_read_reg32(&dev_regs->dctl);</code> 
 */
static __inline__ uint32_t dwc_read_reg32( volatile uint32_t *_reg) 
{
        return readl_relaxed(_reg);
};

/** 
 * Writes a register with a 32 bit value.
 *
 * @param _reg address of register to read.
 * @param _value to write to _reg.
 *
 * Usage:<br>
 * <code>dwc_write_reg32(&dev_regs->dctl, 0); </code>
 */
static __inline__ void dwc_write_reg32( volatile uint32_t *_reg, const uint32_t _value) 
{
        writel_relaxed( _value, _reg );
        dsb();
};

/**  
 * This function modifies bit values in a register.  Using the
 * algorithm: (reg_contents & ~clear_mask) | set_mask.
 *
 * @param _reg address of register to read.
 * @param _clear_mask bit mask to be cleared.
 * @param _set_mask bit mask to be set.
 *
 * Usage:<br> 
 * <code> // Clear the SOF Interrupt Mask bit and <br>
 * // set the OTG Interrupt mask bit, leaving all others as they were.
 *    dwc_modify_reg32(&dev_regs->gintmsk, DWC_SOF_INT, DWC_OTG_INT);</code>
 */
static __inline__
 void dwc_modify_reg32( volatile uint32_t *_reg, const uint32_t _clear_mask, const uint32_t _set_mask) 
{
        writel_relaxed( (readl_relaxed(_reg) & ~_clear_mask) | _set_mask, _reg );  
        dsb();
};


/**
 * Wrapper for the OS micro-second delay function.
 * @param[in] _usecs Microseconds of delay
 */
static __inline__ void UDELAY( const uint32_t _usecs ) 
{
        udelay( _usecs );
}

/**
 * Wrapper for the OS milli-second delay function.
 * @param[in] _msecs milliseconds of delay
 */
static __inline__ void MDELAY( const uint32_t _msecs ) 
{
        mdelay( _msecs );
}

/**
 * Wrapper for the Linux spin_lock.  On the ARM (Integrator)
 * spin_lock() is a nop.
 *
 * @param _lock Pointer to the spinlock.
 */
static __inline__ void SPIN_LOCK( spinlock_t *_lock )  
{
//        spin_lock(_lock);
}

/**
 * Wrapper for the Linux spin_unlock.  On the ARM (Integrator)
 * spin_lock() is a nop.
 *
 * @param _lock Pointer to the spinlock.
 */
static __inline__ void SPIN_UNLOCK( spinlock_t *_lock )     
{ 
//        spin_unlock(_lock);
}

/**
 * Wrapper (macro) for the Linux spin_lock_irqsave.  On the ARM
 * (Integrator) spin_lock() is a nop.
 *
 * @param _l Pointer to the spinlock.
 * @param _f unsigned long for irq flags storage.
 */
#define SPIN_LOCK_IRQSAVE( _l, _f )  { \
	spin_lock_irqsave(_l,_f); \
	}

/**
 * Wrapper (macro) for the Linux spin_unlock_irqrestore.  On the ARM
 * (Integrator) spin_lock() is a nop.
 *
 * @param _l Pointer to the spinlock.
 * @param _f unsigned long for irq flags storage.
 */
#define SPIN_UNLOCK_IRQRESTORE( _l,_f ) {\
	spin_unlock_irqrestore(_l,_f); 	\
	}


/*
 * Debugging support vanishes in non-debug builds.  
 */


/**
 * The Debug Level bit-mask variable.
 */
extern uint32_t g_dbg_lvl;
/**
 * Set the Debug Level variable.
 */
static inline uint32_t SET_DEBUG_LEVEL( const uint32_t _new )
{
        uint32_t old = g_dbg_lvl;
        g_dbg_lvl = _new;
        return old;
}

/** When debug level has the DBG_CIL bit set, display CIL Debug messages. */
#define DBG_CIL		(0x2)
/** When debug level has the DBG_CILV bit set, display CIL Verbose debug
 * messages */
#define DBG_CILV	(0x20)
/**  When debug level has the DBG_PCD bit set, display PCD (Device) debug
 *  messages */
#define DBG_PCD		(0x4)	
/** When debug level has the DBG_PCDV set, display PCD (Device) Verbose debug
 * messages */
#define DBG_PCDV	(0x40)	
/** When debug level has the DBG_HCD bit set, display Host debug messages */
#define DBG_HCD		(0x8)	
/** When debug level has the DBG_HCDV bit set, display Verbose Host debug
 * messages */
#define DBG_HCDV	(0x80)
/** When debug level has the DBG_HCD_URB bit set, display enqueued URBs in host
 *  mode. */
#define DBG_HCD_URB	(0x800)

/** When debug level has any bit set, display debug messages */
#define DBG_ANY		(0xFF)

/** All debug messages off */
#define DBG_OFF		0

/** Prefix string for DWC_DEBUG print macros. */
#define USB_DWC "DWC_OTG: "

/** 
 * Print a debug message when the Global debug level variable contains
 * the bit defined in <code>lvl</code>.
 *
 * @param[in] lvl - Debug level, use one of the DBG_ constants above.
 * @param[in] x - like printf
 *
 *    Example:<p>
 * <code>
 *      DWC_DEBUGPL( DBG_ANY, "%s(%p)\n", __func__, _reg_base_addr);
 * </code>
 * <br>
 * results in:<br> 
 * <code>
 * usb-DWC_otg: dwc_otg_cil_init(ca867000)
 * </code>
 */
#ifdef DEBUG
# define DWC_DEBUGPL(lvl, x...) do{ if ((lvl)&g_dbg_lvl)printk( KERN_DEBUG USB_DWC x ); }while(0) 
//# define DWC_DEBUGPL(lvl, x...) printk( ">>> " x ) 
# define DWC_DEBUGP(x...)	DWC_DEBUGPL(DBG_ANY, x )

# define CHK_DEBUG_LEVEL(level) ((level) & g_dbg_lvl)

#else

# define DWC_DEBUGPL(lvl, x...) do{}while(0)
# define DWC_DEBUGP(x...)

# define CHK_DEBUG_LEVEL(level) (0)

#endif /*DEBUG*/

#if 1
/**
 * Print an Error message.
 */
#define DWC_ERROR(x...) printk( KERN_ERR USB_DWC x )
/**
 * Print a Warning message.
 */
#define DWC_WARN(x...) printk( KERN_WARNING USB_DWC x )
/**
 * Print a notice (normal but significant message).
 */
#define DWC_NOTICE(x...) printk( KERN_NOTICE USB_DWC x )
/**
 *  Basic message printing.
 */
#define DWC_PRINT(x...) printk( KERN_INFO USB_DWC x )
#else
/**
 * Print an Error message.
 */
#define DWC_ERROR(x...) 
/**
 * Print a Warning message.
 */
#define DWC_WARN(x...) 
/**
 * Print a notice (normal but significant message).
 */
#define DWC_NOTICE(x...) 
/**
 *	Basic message printing.
 */
#define DWC_PRINT(x...) 

#endif


#endif
