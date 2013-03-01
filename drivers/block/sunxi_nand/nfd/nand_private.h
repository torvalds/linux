/*
 * drivers/block/sunxi_nand/nfd/nand_private.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef	_NAND_PRIVATE_H_
#define	_NAND_PRIVATE_H_

#include    "../include/type_def.h"
#include	"../src/include/nand_physic.h"
#include	"../src/include/nand_format.h"
#include	"../src/include/nand_logic.h"
#include	"../src/include/nand_scan.h"

extern struct __NandDriverGlobal_t NandDriverInfo;

extern struct __NandStorageInfo_t  NandStorageInfo;

#define DiskSize  (SECTOR_CNT_OF_SINGLE_PAGE * PAGE_CNT_OF_PHY_BLK * BLOCK_CNT_OF_DIE * \
            DIE_CNT_OF_CHIP * NandStorageInfo.ChipCnt  / 1024 * DATA_BLK_CNT_OF_ZONE)

/*
typedef struct
{
	__u8    mid;
	__u32   used;

} __drv_nand_t;

typedef struct
{
	__u32             offset;
	__u8              used;
	char			  major_name[24];
	char              minor_name[24];

   __hdle            hReg;
   device_block info;
   //__dev_blkinfo_t   info;
}__dev_nand_t;
*/
/*
extern __s32 nand_drv_init  (void);
extern __s32 nand_drv_exit  (void);
extern __mp* nand_drv_open(__u32 mid, __u32 mode);
extern __s32 nand_drv_close(__mp * pDev);
extern __u32 nand_drv_read(void * pBuffer, __u32 blk, __u32 n, __mp * pDev);
extern __u32 nand_drv_write(const void * pBuffer, __u32 blk, __u32 n, __mp * pDev) ;
extern __s32 nand_drv_ioctrl(__mp * pDev, __u32 Cmd, __s32 Aux, void *pBuffer);


extern block_device_operations nand_dev_op;
*/


//extern __dev_devop_t nand_dev_op;

#endif /*_NAND_PRIVATE_H_*/

