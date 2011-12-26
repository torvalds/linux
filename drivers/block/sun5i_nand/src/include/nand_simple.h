#ifndef __PHY_BOOT__
#define  __PHY_BOOT__

#include "nand_oal.h"

#define SUCESS	0
#define FAIL	-1;
#define BADBLOCK -2

struct boot_physical_param{
	__u8   chip; //chip no
	__u16  block; // block no within chip
	__u16  page; // apge no within block
	__u16  sectorbitmap; //done't care
	void   *mainbuf; //data buf
	void   *oobbuf; //oob buf
};

extern __s32 PHY_SimpleErase(struct boot_physical_param * eraseop);
extern __s32 PHY_SimpleRead(struct boot_physical_param * readop);
extern __s32 PHY_SimpleWrite(struct boot_physical_param * writeop);
extern __s32 PHY_SimpleWrite_1K(struct boot_physical_param * writeop);
extern __s32 PHY_SimpleWrite_Seq(struct boot_physical_param * writeop);
extern __s32 PHY_SimpleRead_Seq(struct boot_physical_param * readop);
extern __s32 PHY_SimpleRead_1K(struct boot_physical_param * readop);
#endif
