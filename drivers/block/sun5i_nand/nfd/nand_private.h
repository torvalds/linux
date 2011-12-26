#ifndef	_NAND_PRIVATE_H_
#define	_NAND_PRIVATE_H_

#include    "../include/type_def.h"
#include	"../src/include/nand_physic.h"
#include	"../src/include/nand_format.h"
#include	"../src/include/nand_logic.h"
#include	"../src/include/nand_scan.h"

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

