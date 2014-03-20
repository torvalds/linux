/* Driver for Realtek PCI-Express card reader
 * Header file
 *
 * Copyright(c) 2009-2013 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   Wei WANG (wei_wang@realsil.com.cn)
 *   Micky Ching (micky_ching@realsil.com.cn)
 */

#ifndef __REALTEK_RTSX_SPI_H
#define __REALTEK_RTSX_SPI_H

/* SPI operation error */
#define SPI_NO_ERR		0x00
#define SPI_HW_ERR		0x01
#define SPI_INVALID_COMMAND	0x02
#define SPI_READ_ERR		0x03
#define SPI_WRITE_ERR		0x04
#define SPI_ERASE_ERR		0x05
#define SPI_BUSY_ERR		0x06

/* Serial flash instruction */
#define SPI_READ		0x03
#define SPI_FAST_READ		0x0B
#define SPI_WREN		0x06
#define SPI_WRDI		0x04
#define SPI_RDSR		0x05

#define SF_PAGE_LEN		256

#define BYTE_PROGRAM		0
#define AAI_PROGRAM		1
#define PAGE_PROGRAM		2

#define PAGE_ERASE		0
#define CHIP_ERASE		1

int spi_erase_eeprom_chip(struct rtsx_chip *chip);
int spi_erase_eeprom_byte(struct rtsx_chip *chip, u16 addr);
int spi_read_eeprom(struct rtsx_chip *chip, u16 addr, u8 *val);
int spi_write_eeprom(struct rtsx_chip *chip, u16 addr, u8 val);
int spi_get_status(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int spi_set_parameter(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int spi_read_flash_id(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int spi_read_flash(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int spi_write_flash(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int spi_erase_flash(struct scsi_cmnd *srb, struct rtsx_chip *chip);
int spi_write_flash_status(struct scsi_cmnd *srb, struct rtsx_chip *chip);


#endif  /* __REALTEK_RTSX_SPI_H */
