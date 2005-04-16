/*
   --------------------------------------------------------------------
   i2c-ite.h: Global defines for the I2C controller on board the    
                 ITE MIPS processor.                                
   --------------------------------------------------------------------
   Hai-Pao Fan, MontaVista Software, Inc.
   hpfan@mvista.com or source@mvista.com

   Copyright 2001 MontaVista Software Inc.

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

 */

#ifndef I2C_ITE_H
#define I2C_ITE_H 1

#include <asm/it8172/it8172.h>

/* I2C Registers */
#define ITE_I2CHCR	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x30
#define ITE_I2CHSR	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x34
#define ITE_I2CSAR	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x38
#define ITE_I2CSSAR	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x3c
#define ITE_I2CCKCNT	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x48
#define ITE_I2CSHDR	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x4c
#define ITE_I2CRSUR	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x50
#define ITE_I2CPSUR	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x54

#define ITE_I2CFDR	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x70
#define ITE_I2CFBCR	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x74
#define ITE_I2CFCR	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x78
#define ITE_I2CFSR	IT8172_PCI_IO_BASE + IT_I2C_BASE + 0x7c


/* Host Control Register ITE_I2CHCR */
#define	ITE_I2CHCR_HCE	0x01	/* Enable I2C Host Controller */
#define	ITE_I2CHCR_IE	0x02	/* Enable the interrupt after completing
				   the current transaction */
#define ITE_I2CHCR_CP_W	0x00	/* bit2-4 000 - Write */
#define	ITE_I2CHCR_CP_R	0x08	/*	  010 - Current address read */
#define	ITE_I2CHCR_CP_S	0x10	/*	  100 - Sequential read */
#define ITE_I2CHCR_ST	0x20	/* Initiates the I2C host controller to execute
				   the command and send the data programmed in
				   all required registers to I2C bus */
#define ITE_CMD		ITE_I2CHCR_HCE | ITE_I2CHCR_IE | ITE_I2CHCR_ST
#define ITE_WRITE	ITE_CMD | ITE_I2CHCR_CP_W
#define ITE_READ	ITE_CMD | ITE_I2CHCR_CP_R
#define ITE_SREAD	ITE_CMD | ITE_I2CHCR_CP_S

/* Host Status Register ITE_I2CHSR */
#define	ITE_I2CHSR_DB	0x01	/* Device is busy, receives NACK response except
				   in the first and last bytes */
#define	ITE_I2CHSR_DNE	0x02	/* Target address on I2C bus does not exist */
#define	ITE_I2CHSR_TDI	0x04	/* R/W Transaction on I2C bus was completed */
#define	ITE_I2CHSR_HB	0x08	/* Host controller is processing transactions */
#define	ITE_I2CHSR_FER	0x10	/* Error occurs in the FIFO */

/* Slave Address Register ITE_I2CSAR */
#define	ITE_I2CSAR_SA_MASK	0xfe	/* Target I2C device address */
#define	ITE_I2CSAR_ASO		0x0100	/* Output 1/0 to I2CAS port when the
					   next slave address is addressed */

/* Slave Sub-address Register ITE_I2CSSAR */
#define	ITE_I2CSSAR_SUBA_MASK	0xff	/* Target I2C device sub-address */

/* Clock Counter Register ITE_I2CCKCNT */
#define	ITE_I2CCKCNT_STOP	0x00	/* stop I2C clock */
#define	ITE_I2CCKCNT_HPCC_MASK	0x7f	/* SCL high period counter */
#define	ITE_I2CCKCNT_LPCC_MASK	0x7f00	/* SCL low period counter */

/* START Hold Time Register ITE_I2CSHDR */
/* value is counted based on 16 MHz internal clock */
#define ITE_I2CSHDR_FM	0x0a	/* START condition at fast mode */
#define	ITE_I2CSHDR_SM	0x47	/* START contition at standard mode */

/* (Repeated) START Setup Time Register ITE_I2CRSUR */
/* value is counted based on 16 MHz internal clock */
#define	ITE_I2CRSUR_FM	0x0a	/* repeated START condition at fast mode */
#define	ITE_I2CRSUR_SM	0x50	/* repeated START condition at standard mode */

/* STOP setup Time Register ITE_I2CPSUR */

/* FIFO Data Register ITE_I2CFDR */
#define	ITE_I2CFDR_MASK		0xff

/* FIFO Byte Count Register ITE_I2CFBCR */
#define ITE_I2CFBCR_MASK	0x3f

/* FIFO Control Register ITE_I2CFCR */
#define	ITE_I2CFCR_FLUSH	0x01	/* Flush FIFO and reset the FIFO point
					   and I2CFSR */
/* FIFO Status Register ITE_I2CFSR */
#define	ITE_I2CFSR_FO	0x01	/* FIFO is overrun when write */
#define	ITE_I2CFSR_FU	0x02	/* FIFO is underrun when read */
#define	ITE_I2CFSR_FF	0x04	/* FIFO is full when write */
#define	ITE_I2CFSR_FE	0x08	/* FIFO is empty when read */

#endif  /* I2C_ITE_H */
