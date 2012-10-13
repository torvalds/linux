/*
 * DesignWare HS OTG controller driver
 * Copyright (C) 2006 Synopsys, Inc.
 * Portions Copyright (C) 2010 Applied Micro Circuits Corporation.
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see http://www.gnu.org/licenses
 * or write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Suite 500, Boston, MA 02110-1335 USA.
 *
 * Based on Synopsys driver version 2.60a
 * Modified by Mark Miesfeld <mmiesfeld@apm.com>
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING BUT NOT LIMITED TO THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL SYNOPSYS, INC. BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES
 * (INCLUDING BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#if !defined(__DWC_OTG_DRIVER_H__)
#define __DWC_OTG_DRIVER_H__

/*
 * This file contains the interface to the Linux driver.
 */
#include "cil.h"

/*
 * This structure is a wrapper that encapsulates the driver components used to
 * manage a single DWC_otg controller.
 */
struct dwc_otg_device {
	/* Base address returned from ioremap() */
	__iomem void *base;

	/* Pointer to the core interface structure. */
	struct core_if *core_if;

	/* Pointer to the PCD structure. */
	struct dwc_pcd *pcd;

	/* Pointer to the HCD structure. */
	struct dwc_hcd *hcd;

	/* Flag to indicate whether the common IRQ handler is installed. */
	u8 common_irq_installed;

	/* Interrupt request number. */
	unsigned int irq;

	/*
	 * Physical address of Control and Status registers, used by
	 * release_mem_region().
	 */
	resource_size_t phys_addr;

	spinlock_t lock;

	/* Length of memory region, used by release_mem_region(). */
	unsigned long base_len;
};
#endif
