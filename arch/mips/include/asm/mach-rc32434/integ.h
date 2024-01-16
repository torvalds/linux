/*
 *  Definitions for the Watchdog registers
 *
 *  Copyright 2002 Ryan Holm <ryan.holmQVist@idt.com>
 *  Copyright 2008 Florian Fainelli <florian@openwrt.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __RC32434_INTEG_H__
#define __RC32434_INTEG_H__

#include <asm/mach-rc32434/rb.h>

#define INTEG0_BASE_ADDR	0x18030030

struct integ {
	u32 errcs;			/* sticky use ERRCS_ */
	u32 wtcount;			/* Watchdog timer count reg. */
	u32 wtcompare;			/* Watchdog timer timeout value. */
	u32 wtc;			/* Watchdog timer control. use WTC_ */
};

/* Error counters */
#define RC32434_ERR_WTO		0
#define RC32434_ERR_WNE		1
#define RC32434_ERR_UCW		2
#define RC32434_ERR_UCR		3
#define RC32434_ERR_UPW		4
#define RC32434_ERR_UPR		5
#define RC32434_ERR_UDW		6
#define RC32434_ERR_UDR		7
#define RC32434_ERR_SAE		8
#define RC32434_ERR_WRE		9

/* Watchdog control bits */
#define RC32434_WTC_EN		0
#define RC32434_WTC_TO		1

#endif	/* __RC32434_INTEG_H__ */
