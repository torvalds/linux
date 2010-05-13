/* drivers/i2c/busses/i2c_rk2818.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __RK2818_I2C_H
#define __RK2818_I2C_H

/* master transmit */
#define I2C_MTXR                (0x0000)
/* master receive */
#define I2C_MRXR                (0x0004)
/* slave address */
#define I2C_SADDR               (0x0010)
/* interrupt enable control */
#define I2C_IER                 (0x0014)
#define I2C_IER_ARBITR_LOSE     (1<<7)
#define I2C_IER_MRX_NEED_ACK    (1<<1)
#define I2C_IER_MTX_RCVD_ACK    (1<<0)

#define IRQ_MST_ENABLE        	(I2C_IER_ARBITR_LOSE | \
		                         I2C_IER_MRX_NEED_ACK | \
		                         I2C_IER_MTX_RCVD_ACK)
#define IRQ_ALL_DISABLE         (0x00)

/* interrupt status, write 0 to clear */
#define I2C_ISR                 (0x0018)
#define I2C_ISR_ARBITR_LOSE     (1<<7)
#define I2C_ISR_MRX_NEED_ACK    (1<<1)
#define I2C_ISR_MTX_RCVD_ACK    (1<<0)

/* stop/start/resume command, write 1 to set */
#define I2C_LCMR                (0x001c)
#define I2C_LCMR_RESUME         (1<<2)
#define I2C_LCMR_STOP           (1<<1)
#define I2C_LCMR_START          (1<<0)

/* i2c core status */
#define I2C_LSR                 (0x0020)
#define I2C_LSR_RCV_NAK         (1<<1)
#define I2C_LSR_RCV_ACK         (~(1<<1))
#define I2C_LSR_BUSY            (1<<0)

/* i2c config */
#define I2C_CONR                (0x0024)
#define I2C_CONR_NAK    	    (1<<4)
#define I2C_CONR_ACK	         (~(1<<4))
#define I2C_CONR_MTX_MODE       (1<<3)
#define I2C_CONR_MRX_MODE       (~(1<<3))
#define I2C_CONR_MPORT_ENABLE   (1<<2)
#define I2C_CONR_MPORT_DISABLE  (~(1<<2))

/* i2c core config */
#define I2C_OPR                 (0x0028)
#define I2C_OPR_RESET_STATUS    (1<<7)
#define I2C_OPR_CORE_ENABLE     (1<<6)

#define I2CCDVR_REM_BITS        (0x03)
#define I2CCDVR_REM_MAX         (1<<(I2CCDVR_REM_BITS))
#define I2CCDVR_EXP_BITS        (0x03)
#define I2CCDVR_EXP_MAX         (1<<(I2CCDVR_EXP_BITS))

#endif
