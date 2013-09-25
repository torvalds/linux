#include "app_i2c_lib_icatch.h"
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include "icatch7002_common.h"

#define SPI_CMD_BYTE_READ 			0x03
#define SPI_CMD_RD_ID 				0x9F
#define	SPI_CMD_WRT_EN				0x06
#define SPI_CMD_BYTE_PROG 			0x02
#define SPI_CMD_RD_STS 				0x05
#define SPI_CMD_BYTE_PROG_AAI 		0xAD
#define SPI_CMD_WRT_STS_EN  		0x50
#define SPI_CMD_WRT_STS 			0x01
#define SPI_CMD_WRT_DIS 			0x04
#define	SPI_CMD_ERASE_ALL			0xC7
#define	SPI_CMD_SECTOR_ERASE		0x20
#define	SPI_CMD_32KB_BLOCK_ERASE	0x52
#define	SPI_CMD_64KB_BLOCK_ERASE	0xD8
#define WAIT_COUNT 100

#define I2CDataWrite(reg,val) icatch_sensor_write((reg),(val))
#define I2CDataRead(reg)  icatch_sensor_read((reg))
#define osMemAlloc(len) kzalloc((len), GFP_KERNEL);
#define osMemFree(pbuf) kfree(pbuf)
#define seqI2CDataRead(reg,buf)  *((u8*)(buf))= icatch_sensor_read(reg)
#define seqI2CDataWrite(reg,buf)  icatch_sensor_write((reg),(*(u8*)(buf)))

#define hsI2CDataRead(reg)  icatch_sensor_read((reg))
#define hsI2CDataWrite(reg,val) icatch_sensor_write((reg),(val)) 

#ifndef tmrUsDelay
#define tmrUsWait(ulTime)	(((ulTime)>1000)?(msleep((ulTime)/1000)):udelay((ulTime)))
#endif
#define ros_thread_sleep(ulTime) (((ulTime)>1000)?(msleep((ulTime)/1000)):udelay((ulTime)))


//local declare
void I2C_SPIInit(void);
UINT32 I2C_SPIFlashReadId(void);
UINT32 BB_SerialFlashTypeCheck(UINT32 id,UINT32 *spiSize);
void BB_EraseSPIFlash(UINT32 type,UINT32 spiSize);
UINT32 I2C_SPISstFlashWrite(UINT32 addr,UINT32 pages,UINT8 *pbuf);
UINT32 I2C_SPIFlashWrite_DMA(UINT32 addr,UINT32 pages,UINT32 usage,UINT8 *pbuf);
UINT32 I2C_SPI64KBBlockErase(UINT32 address,UINT32 stFlag);
UINT32 I2C_SPI32KBBlockErase(UINT32 address,UINT32 stFlag);
UINT32 I2C_SPISectorErase(UINT32 address,UINT32 stFlag);

static const UINT32 stSpiIdInfo[7][3] =
{
	/*Winbond*/
	{0x00EF3017,4096, 2048},
	{0x00EF3016,4096, 1024},
	{0x00EF3015,4096, 512},
	{0x00EF3014,4096, 256},
	{0x00EF5014,4096, 256},
	{0x00EF3013,4096, 128},
	{0x00EF5013,4096, 128},
	/*Fail*/
	{0x00000000,0,0},
};

static const UINT32 sstSpiIdInfo[6][3] =
{
	/*ESMT*/
	{0x008C4016,4096,512},
	/*SST*/
	{0x00BF254A,4096,1024},
	{0x00BF2541,4096,512},
	{0x00BF258E,4096,256},
	{0x00BF258D,4096,128},
	/*Fail*/
	{0x00000000,0,0},
};

/* BB_WrSPIFlash ¬° main function for burning SPI */
UINT32 I2C_SPIStChipErase(
	void
);

void
BB_WrSPIFlash(
	UINT32 size
)
{
  UINT32 id, type;
	UINT32 pages, spiSize;

	UINT32 fd, fileSize;
	UINT8* pbootBuf = NULL;
	const struct firmware *fw = NULL;

	printk("loadcode from file\n");
	/*#define FILE_PATH_BOOTCODE "D:\\FPGA.IME"*/
	//#define FILE_PATH_BOOTCODE "D:\\DCIM\\BOOT.BIN"

	//fd = sp5kFsFileOpen( FILE_PATH_BOOTCODE, SP5K_FS_OPEN_RDONLY );
	#if 0
	if( icatch_request_firmware(&fw) !=0){
		printk("%s:%d,requst firmware failed!!\n",__func__,__LINE__);
		goto out;
		}
	#endif
	I2CDataWrite(0x70c4,0x00);
	I2CDataWrite(0x70c5,0x00);
	
	I2CDataWrite(0x1011,0x01); /* CPU Reset */
	I2CDataWrite(0x001C,0x08);/* FM reset */
	I2CDataWrite(0x001C,0x00);
	I2CDataWrite(0x108C,0x00);/* DMA select */
	I2CDataWrite(0x009a,0x00);/*CPU normal operation */
#if 0
	if(fd != 0)
	{
		fileSize = sp5kFsFileSizeGet(fd);
		printk("fileSize:0x%x\n",fileSize);
		pbootBuf= (UINT8*)sp5kMalloc(fileSize);
		sp5kFsFileRead(fd, pbootBuf, fileSize);
		printk("pbootBuf:0x%x\n",pbootBuf);
		fsFileClose(fd);
	}
	else
	{
		printk("file open error\n");
		return;
	}
#endif
	fileSize = fw->data;
	pbootBuf = fw->size;
	if(pbootBuf == NULL)
	{
		printk("buffer allocate failed\n");
		goto out;
	}

   	I2C_SPIInit();

	id = I2C_SPIFlashReadId();
	if(id==0)
	{
		printk("read id failed\n");
		goto out;
	}
	/*printk("spiSize:0x%x\n",&spiSize);*/
	type = BB_SerialFlashTypeCheck(id, &spiSize);
	if( type == 0 )
	{
		printk("read id failed\n");
	 	goto out;
	}

	if( size > 0 && size < fileSize )
	{
		pages = size/0x100;
		if((size%0x100)!=0)
			pages += 1;
	}
	else
	{
		pages = fileSize/0x100;
	}
	/*printk("pages:0x%x\n",pages);*/

	//BB_EraseSPIFlash(type,spiSize);
	I2C_SPIStChipErase();
	msleep(4*1000);
	if( type == 2 )
	{
		printk("SST operation\n");
		I2C_SPISstFlashWrite(0,pages,pbootBuf);
	}
	else if( type == 1 || type == 3 )
	{
		printk("ST operation\n");
		I2C_SPIFlashWrite_DMA(0,pages,1,pbootBuf);
	}
out:
	if(fw)
		icatch_release_firmware(fw);
	return;

//	osMemFree(pbootBuf);
}

UINT32
BB_SerialFlashTypeCheck(
	UINT32 id,
	UINT32 *spiSize
)
{
	UINT32 i=0;
	UINT32 fullID = 1;
	UINT32 shift = 0, tblId, type = 0;
	/*printk("id:0x%x spiSize:0x%x\n",id,spiSize);*/
	/* check whether SST type serial flash */
	while( 1 ){
		tblId = sstSpiIdInfo[i][0] >> shift;
		if( id == tblId ) {
			printk("SST type serial flash:%x %x %x\n",i,id,sstSpiIdInfo[i][0]);
			type = 2;
			*spiSize = sstSpiIdInfo[i][1]*sstSpiIdInfo[i][2];
			break;
		}
		if( id == 0x00FFFFFF || id == 0x00000000) {
			return 0;
		}
		if( sstSpiIdInfo[i][0] == 0x00000000 ) {
			#if 0
			if( fullID ){
				fullID = 0;/* sarch partial ID */
				i = 0;
				shift = 16;
				id = id >> shift;
				continue;
			}
			#endif
			type = 3;
			break;
		}
		i ++;
	}
	if( type == 2 )
		return type;

	i = 0;
	/* check whether ST type serial flash */
	while( 1 ){
		tblId = stSpiIdInfo[i][0] >> shift;
		if( id == tblId ) {
			printk("ST Type serial flash:%x %x %x\n",i,id,stSpiIdInfo[i][0]);
			type = 1;
			*spiSize = stSpiIdInfo[i][1]*stSpiIdInfo[i][2];
			/*printk("spiSize:0x%x\n",*spiSize);*/
			break;
		}
		if( id == 0x00FFFFFF || id == 0x00000000) {
			return 0;
		}
		if( stSpiIdInfo[i][0] == 0x00000000 ) {
			if( fullID ){
				fullID = 0;/* sarch partial ID */
				i = 0;
				shift = 16;
				id = id >> shift;
				continue;
			}
			type = 3;
			break;
		}
		i ++;
	}

	return type;
}

void
BB_EraseSPIFlash(
	UINT32 type,
	UINT32 spiSize
)
{
	UINT8 typeFlag;
	UINT32 i, temp1;
	if( type == 2 )/* SST */
	{
		typeFlag = 0;
	}
	else if( type == 1 || type == 3 )/* ST */
	{
		typeFlag = 1;
	}
	/*printk("spiSize:0x%x\n",spiSize);*/
	if(spiSize == (512*1024))
	{
		/* skip 0x7B000 ~ 0x7EFF, to keep calibration data */
		temp1 = (spiSize / 0x10000)-1;
		for(i=0;i<temp1;i++)
		{
			I2C_SPI64KBBlockErase(i*0x10000,typeFlag);
		}
		I2C_SPI32KBBlockErase(temp1*0x10000,typeFlag);
		temp1 = temp1*0x10000 + 0x8000;
		for(i=temp1;i<spiSize-0x5000;i+=0x1000)
		{
			I2C_SPISectorErase(i,typeFlag);
		}
		I2C_SPISectorErase(spiSize-0x1000,typeFlag);
	}
	else if(spiSize == (1024*1024))
	{
		/* only erase 256*3KB */
		temp1 = (spiSize / 0x10000)-2;
		for(i=0;i<temp1;i++)
		{
			I2C_SPI64KBBlockErase(i*0x10000,typeFlag);
		}
		I2C_SPI32KBBlockErase((temp1)*0x10000,typeFlag);
		I2C_SPISectorErase(spiSize-0x1000,typeFlag);
	}
}

/*-------------------------------------------------------------------------
 *  File Name : I2C_SPIInit
 *  return none
 *------------------------------------------------------------------------*/
void I2C_SPIInit(void)
{
    UINT32 temp;

    /*temp = I2CDataRead(0x0026);*/
    I2CDataWrite(0x0026,0xc0);
    I2CDataWrite(0x4051,0x01); /* spien */
    I2CDataWrite(0x40e1,0x00); /* spi mode */
    I2CDataWrite(0x40e0,0x12); /* spi freq */	
}

/*-------------------------------------------------------------------------
 *  File Name : I2C_SPIInit
 *  return SUCCESS: normal
           FAIL: if wait spi flash time out
 *------------------------------------------------------------------------*/
UINT32 I2C_SPIFlashPortWait(void)
{
    UINT32 cnt = WAIT_COUNT;
#if 0
    while(I2CDataRead(0x40e6) != 0x00){
        cnt--;
        if(cnt == 0x00)
        {
            printk("serial flash port wait time out!!\n");
            return FAIL;
        }
    }
#endif
    return SUCCESS;
}

/*-------------------------------------------------------------------------
 *  File Name : I2C_SPIFlashPortWrite
 *  return SUCCESS: normal
           FAIL:    if wait spi flash time out
 *------------------------------------------------------------------------*/
UINT32 I2C_SPIFlashPortWrite(UINT32 wData)
{
    hsI2CDataWrite(0x40e3,(UINT8)wData);
    return I2C_SPIFlashPortWait();
}

/*-------------------------------------------------------------------------
 *  File Name : I2C_SPIFlashPortRead
 *  return SUCCESS: normal
           FAIL:    if wait spi flash time out
 *------------------------------------------------------------------------*/
UINT32 I2C_SPIFlashPortRead(void)
{
    UINT32 ret;

    ret = hsI2CDataRead(0x40e4);
    /* polling SPI state machine ready */
    if (I2C_SPIFlashPortWait() != SUCCESS) {
        return 0;
    }
	tmrUsWait(10);
    ret = hsI2CDataRead(0x40e5);

    return ret;
}

/*-------------------------------------------------------------------------
 *  File Name : I2C_SPIFlashReadId
 *  return SPI flash ID
           0: if read ID failed
 *------------------------------------------------------------------------*/
UINT32 I2C_SPIFlashReadId(void)
{
    UINT8 id[3];
    UINT32 ID;
    UINT32 err;
    
    id[0] = 0;
    id[1] = 0;
    id[2] = 0;
    
    hsI2CDataWrite(0x40e7,0x00);
    
    err = I2C_SPIFlashPortWrite(SPI_CMD_RD_ID); /*read ID command*/
    if (err != SUCCESS) {
        printk("Get serial flash ID failed\n");
        return 0;
    }
    
    id[0] = I2C_SPIFlashPortRead();    /* Manufacturer's  ID */
    id[1] = I2C_SPIFlashPortRead();    /* Device ID          */
    id[2] = I2C_SPIFlashPortRead();    /* Manufacturer's  ID */ 
    
    hsI2CDataWrite(0x40e7,0x01);
    
    printk("ID %2x %2x %2x\n", id[0], id[1], id[2]);
    
    ID = ((UINT32)id[0] << 16) | ((UINT32)id[1] << 8) | \
    ((UINT32)id[2] << 0);
    
    return ID;
}

/*-------------------------------------------------------------------------
 *  File Name : I2C_SPIFlashWrEnable
 *  return none
 *------------------------------------------------------------------------*/
void I2C_SPIFlashWrEnable(void)
{
    hsI2CDataWrite(0x40e7,0x00);
    I2C_SPIFlashPortWrite(SPI_CMD_WRT_EN);
    hsI2CDataWrite(0x40e7,0x01);
}

/*-------------------------------------------------------------------------
 *  File Name : I2C_SPIStsRegRead
 *  return ret
 *------------------------------------------------------------------------*/
UINT32 I2C_SPIStsRegRead(void)
{
    UINT32 ret;
    
    hsI2CDataWrite(0x40e7,0x00);
    
    I2C_SPIFlashPortWrite(SPI_CMD_RD_STS);
    ret = I2C_SPIFlashPortRead();
    
    hsI2CDataWrite(0x40e7,0x01);
    
    return ret;
}

/*-------------------------------------------------------------------------
 *  File Name : I2C_SPITimeOutWait
 *  return none
 *------------------------------------------------------------------------*/
void I2C_SPITimeOutWait(UINT32 poll, UINT32 *ptimeOut)
{
    /* MAX_TIME for SECTOR/BLOCK ERASE is 25ms */
    UINT32 sts;
    UINT32 time = 0;
    while (1) {
        sts = I2C_SPIStsRegRead();
        if (!(sts & poll))	/* sfStatusRead() > 4.8us */ {
            break;
        }
        time ++;
        if( *ptimeOut < time ) {
            printk("TimeOut %d, sts=0x%x, poll=0x%x\n",time,sts,poll);
            break;
        }
    }
}

UINT32 I2C_SPIStChipErase(
	void
)
{
	UINT32 timeout;
	printk("ST Chip Erasing...\n");
	
	hsI2CDataWrite(0x40e7,0x00);
	I2C_SPIFlashPortWrite(SPI_CMD_WRT_STS);
	I2C_SPIFlashPortWrite(0x02);
	hsI2CDataWrite(0x40e7,0x01);
	
	I2C_SPIFlashWrEnable();	
	hsI2CDataWrite(0x40e7,0x00);
	I2C_SPIFlashPortWrite(SPI_CMD_ERASE_ALL);
	hsI2CDataWrite(0x40e7,0x01);
	
	timeout = 0xffffffff;
	I2C_SPITimeOutWait(0x01, &timeout);
	ros_thread_sleep(1);
	hsI2CDataWrite(0x40e7,0x01);
	return SUCCESS;
}


UINT32 I2C_SPISstChipErase(
	void
)
{
	UINT32 timeout;
	printk("SST Chip Erasing...\n");

	I2C_SPIFlashWrEnable();
	
	hsI2CDataWrite(0x40e7,0x00);
	I2C_SPIFlashPortWrite(SPI_CMD_WRT_STS_EN);				/*Write Status register command*/
	hsI2CDataWrite(0x40e7,0x01);
	
	hsI2CDataWrite(0x40e7,0x00);
	I2C_SPIFlashPortWrite(SPI_CMD_WRT_STS);
	I2C_SPIFlashPortWrite(0x02);
	hsI2CDataWrite(0x40e7,0x01);

	I2C_SPIFlashWrEnable();
	hsI2CDataWrite(0x40e7,0x00);
	I2C_SPIFlashPortWrite(SPI_CMD_ERASE_ALL);
	hsI2CDataWrite(0x40e7,0x01);

	timeout = 0xffffffff;
	I2C_SPITimeOutWait(0x01, &timeout);
	
	return SUCCESS;
}

UINT32 I2C_SPISectorErase(
	UINT32 address,
	UINT32 stFlag
)
{
	UINT32 timeout;
	printk("addr:0x%x\n",address);
	if(!stFlag)
	{
		I2C_SPIFlashWrEnable();
	
		hsI2CDataWrite(0x40e7,0x00);
		I2C_SPIFlashPortWrite(SPI_CMD_WRT_STS_EN);				/*Write Status register command*/
		hsI2CDataWrite(0x40e7,0x01);
	}
	
	hsI2CDataWrite(0x40e7,0x00);
	I2C_SPIFlashPortWrite(SPI_CMD_WRT_STS);				/*Write Status register command*/
	I2C_SPIFlashPortWrite(0x02);
	hsI2CDataWrite(0x40e7,0x01);

	I2C_SPIFlashWrEnable();

	hsI2CDataWrite(0x40e7,0x00);
	I2C_SPIFlashPortWrite(SPI_CMD_SECTOR_ERASE);
	I2C_SPIFlashPortWrite(address >> 16);	/* A23~A16 */
	I2C_SPIFlashPortWrite(address >> 8);		/* A15~A08 */
	I2C_SPIFlashPortWrite(address);			/* A07~A00 */
	hsI2CDataWrite(0x40e7,0x01);
	
	timeout = 5000000;
	I2C_SPITimeOutWait(0x01, &timeout);
	
	return SUCCESS;
}

UINT32 I2C_SPI32KBBlockErase(
	UINT32 address,
	UINT32 stFlag
)
{
	UINT32 timeout;
	printk("addr:0x%x\n",address);
	if(!stFlag)
	{
		I2C_SPIFlashWrEnable();
	
		hsI2CDataWrite(0x40e7,0x00);
		I2C_SPIFlashPortWrite(SPI_CMD_WRT_STS_EN);				/*Write Status register command*/
		hsI2CDataWrite(0x40e7,0x01);
	}

	hsI2CDataWrite(0x40e7,0x00);
	I2C_SPIFlashPortWrite(SPI_CMD_WRT_STS);				/*Write Status register command*/
	I2C_SPIFlashPortWrite(0x02);
	hsI2CDataWrite(0x40e7,0x01);

	I2C_SPIFlashWrEnable();

	hsI2CDataWrite(0x40e7,0x00);
	I2C_SPIFlashPortWrite(SPI_CMD_32KB_BLOCK_ERASE);
	I2C_SPIFlashPortWrite(address >> 16);	/* A23~A16 */
	I2C_SPIFlashPortWrite(address >> 8);		/* A15~A08 */
	I2C_SPIFlashPortWrite(address);			/* A07~A00 */
	hsI2CDataWrite(0x40e7,0x01);
	
	timeout = 5000000;
	I2C_SPITimeOutWait(0x01, &timeout);
	
	return SUCCESS;
}

UINT32 I2C_SPI64KBBlockErase(
	UINT32 address,
	UINT32 stFlag
)
{
	UINT32 timeout;
	printk("addr:0x%x\n",address);
	if(!stFlag)
	{
		I2C_SPIFlashWrEnable();
	
		hsI2CDataWrite(0x40e7,0x00);
		I2C_SPIFlashPortWrite(SPI_CMD_WRT_STS_EN);				/*Write Status register command*/
		hsI2CDataWrite(0x40e7,0x01);
	}

	hsI2CDataWrite(0x40e7,0x00);
	I2C_SPIFlashPortWrite(SPI_CMD_WRT_STS);				/*Write Status register command*/
	I2C_SPIFlashPortWrite(0x02);
	hsI2CDataWrite(0x40e7,0x01);

	I2C_SPIFlashWrEnable();

	hsI2CDataWrite(0x40e7,0x00);
	I2C_SPIFlashPortWrite(SPI_CMD_64KB_BLOCK_ERASE);
	I2C_SPIFlashPortWrite(address >> 16);	/* A23~A16 */
	I2C_SPIFlashPortWrite(address >> 8);		/* A15~A08 */
	I2C_SPIFlashPortWrite(address);			/* A07~A00 */
	hsI2CDataWrite(0x40e7,0x01);
	
	timeout = 5000000;
	I2C_SPITimeOutWait(0x01, &timeout);
	
	return SUCCESS;
}


/*-------------------------------------------------------------------------
 *  File Name : I2C_SPIFlashWrite
 *  addr: SPI flash starting address
    pages: pages size for data -> datasize = pages * pagesize(0x100)
    pbuf: data buffer
 *  return SUCCESS: normal finish
           FAIL:    write failed
 *------------------------------------------------------------------------*/
UINT32 I2C_SPIFlashWrite(
	UINT32 addr,
	UINT32 pages,
	UINT8 *pbuf
)
{
    UINT32 i, err = SUCCESS;
    UINT32 pageSize = 0x100;
    UINT32 timeout = 100000;
	UINT32 rsvSec1, rsvSec2;

	rsvSec1 = pages*pageSize - 0x5000;
	rsvSec2 = pages*pageSize - 0x1000;
    addr = addr * pageSize;
    
    printk("ST type writing...\n");
    while( pages ) {
        if((pages%0x40)==0)printk("page:0x%x\n",pages);
		if((addr>=rsvSec1) && (addr <rsvSec2))
		{
			addr += 0x1000;
            pbuf += 0x1000;
			pages -= 0x10;
			continue;
		}
		if((pages==1))
		{
			for (i = 0; i < pageSize ; i++) {
            	printk("%2x ",*(pbuf+i));
				if((i%0x10)==0x0f) printk("\n");
       		}
		}
        I2C_SPIFlashWrEnable();
        hsI2CDataWrite(0x40e7,0x00);
        I2C_SPIFlashPortWrite(SPI_CMD_BYTE_PROG);               /* Write one byte command*/
        I2C_SPIFlashPortWrite((UINT8)(addr >> 16));               /* Send 3 bytes address*/
        I2C_SPIFlashPortWrite((UINT8)(addr >> 8));
        I2C_SPIFlashPortWrite((UINT8)(addr));

        for (i = 0; i < pageSize ; i++) {
            I2C_SPIFlashPortWrite(*pbuf);
            pbuf++;
        }
        hsI2CDataWrite(0x40e7,0x01);
        addr += pageSize;
        pages --;
		tmrUsWait(400);
    }
    
    return err;
}

void I2C_SPISstStatusWrite(UINT8 dat)
{
    UINT32 timeout, poll;
    
    I2C_SPIFlashWrEnable();
    
    hsI2CDataWrite(0x40e7,0x00);
    I2C_SPIFlashPortWrite(SPI_CMD_WRT_STS_EN);
    hsI2CDataWrite(0x40e7,0x01);
    
    hsI2CDataWrite(0x40e7,0x00);
    I2C_SPIFlashPortWrite(SPI_CMD_WRT_STS);
    I2C_SPIFlashPortWrite(dat);
    hsI2CDataWrite(0x40e7,0x01);
    
    poll = 0x01;

    timeout = 100000;
    I2C_SPITimeOutWait(poll, &timeout);
    return;
}

void I2C_SPIStStatusWrite(
	UINT8 value,
	UINT8 value2
)
{
    I2C_SPIFlashWrEnable();
	
	#if 1
    hsI2CDataWrite(0x40e7,0x00);
	//has not implement spi related interface,zyc
//	halSpiPortWrite(SPI_CMD_WRT_STS);				/*Write Status register command*/
//	halSpiPortWrite(value);
//	halSpiPortWrite(value2);
    hsI2CDataWrite(0x40e7,0x01);
	
    hsI2CDataWrite(0x40e7,0x00);
    I2C_SPIFlashPortWrite(0x04);
    hsI2CDataWrite(0x40e7,0x01);
	#endif
}

UINT32 I2C_SPISstFlashWrite(
	UINT32 addr,
	UINT32 pages,
	UINT8 *pbuf
)
{
    UINT32 i, err = SUCCESS;
    UINT32 pageSize = 0x100;
    UINT32 timeout = 100000;
    
    addr = addr * pageSize;
    
    printk("SST type writing...\n");
    I2C_SPISstStatusWrite(0x40);
    while( pages ) {
        printk("page:0x%x\n",pages);
		if((addr>=0x7C000) && (addr <0x7F000))
		{
			addr += 0x1000;
			pages -= 0x10;
			continue;
		}
        I2C_SPIFlashWrEnable();
        hsI2CDataWrite(0x40e7,0x00);
        I2C_SPIFlashPortWrite(SPI_CMD_BYTE_PROG_AAI);               /* Write one byte command*/
        I2C_SPIFlashPortWrite((UINT8)(addr >> 16));               /* Send 3 bytes address*/
        I2C_SPIFlashPortWrite((UINT8)(addr >> 8));
        I2C_SPIFlashPortWrite((UINT8)(addr));
        I2C_SPIFlashPortWrite(*pbuf);
        pbuf++;
        I2C_SPIFlashPortWrite(*pbuf);
        pbuf++;
        hsI2CDataWrite(0x40e7,0x01);
        timeout = 100000;
        I2C_SPITimeOutWait(0x01,&timeout);

        for (i = 2; i < pageSize ; i = i+2) {
    		    hsI2CDataWrite(0x40e7,0x00);
            	I2C_SPIFlashPortWrite(SPI_CMD_BYTE_PROG_AAI); 
    		    I2C_SPIFlashPortWrite(*pbuf);
    		    pbuf++;
    		    I2C_SPIFlashPortWrite(*pbuf);
    		    pbuf++;
    		    hsI2CDataWrite(0x40e7,0x01);
    		    timeout = 100000;
    		    I2C_SPITimeOutWait(0x01,&timeout);
    	  }

    	  hsI2CDataWrite(0x40e7,0x00);
    	  I2C_SPIFlashPortWrite(SPI_CMD_WRT_DIS);
    	  hsI2CDataWrite(0x40e7,0x01);

    	  addr += pageSize;
    	  pages --;
    
    	  hsI2CDataWrite(0x40e7,0x00);
    	  I2C_SPIFlashPortWrite(SPI_CMD_WRT_DIS);
    	  hsI2CDataWrite(0x40e7,0x01);
    }
    
    return err;
}


/*-------------------------------------------------------------------------
 *  File Name : I2C_SPIFlashRead
 *  addr: SPI flash starting address
    pages: pages size for data -> datasize = pages * pagesize(0x100)
    pbuf: data buffer
 *  return SUCCESS: normal finish
           FAIL:    read failed
 *------------------------------------------------------------------------*/
UINT32 I2C_SPIFlashRead(
	UINT32 addr,
	UINT32 pages,
	UINT8 *pbuf
)
{
    UINT8* pbufR;
    UINT32 ch, err = SUCCESS;
    UINT32 i, ret, count=0, size=0, bytes, offset;
    UINT32 pageSize = 0x100;
    
    
    addr = addr * pageSize;
    size = pages*pageSize;
    
    I2CDataWrite(0x40e7,0x00);
    I2C_SPIFlashPortWrite(SPI_CMD_BYTE_READ);               /* Write one byte command*/
    I2C_SPIFlashPortWrite((UINT8)(addr >> 16));               /* Send 3 bytes address*/
    I2C_SPIFlashPortWrite((UINT8)(addr >> 8));
    I2C_SPIFlashPortWrite((UINT8)(addr));
    
    for (i = 0; i < size ; i++) {
        *pbuf = I2C_SPIFlashPortRead();
        if((i%256)==0)
            printk("page:0x%x\n",(i/256));
        pbuf ++;
    }
    
    I2CDataWrite(0x40e7,0x01);
    
    return err;
}

UINT32 I2C_7002DmemWr(
	UINT32 bankNum,
	UINT32 byteNum,
	UINT8* pbuf
)
{
	UINT32 i, bank;

	bank = 0x40+bankNum;
	I2CDataWrite(0x10A6,bank);

	//for(i=0;i<byteNum;i+=4)
	
	for(i=0;i<byteNum;i++)
	{
		seqI2CDataWrite((0x1800+i),(pbuf+i)); /* sequentially write DMEM */
	}

	bank = 0x40 + ((bankNum+1)%2);
	hsI2CDataWrite(0x10A6,bank);
}

/* usage: 1 will skip writing reserved block of calibration data
          0 will write reserved block of calibration data */
UINT32 I2C_SPIFlashWrite_DMA(
	UINT32 addr,
	UINT32 pages,
	UINT32 usage,
	UINT8 *pbuf
)
{
    UINT32 i, temp, err = SUCCESS;
    UINT32 pageSize = 0x100, size;
    UINT32 timeout = 100000;
    UINT32 rsvSec1, rsvSec2;
    UINT32 dmemBank = 0;
    UINT32 chk1=0, chk2=0;
    UINT32 count = 0;

    rsvSec1 = pages*pageSize - 0x7000;
    rsvSec2 = pages*pageSize - 0x1000;
    addr = addr * pageSize;

    /* Set DMA bytecnt as 256-1 */
    I2CDataWrite(0x4170,0xff);
    I2CDataWrite(0x4171,0x00);
    I2CDataWrite(0x4172,0x00);

    /* Set DMA bank & DMA start address */
    I2CDataWrite(0x1084,0x01);
    I2CDataWrite(0x1080,0x00);
    I2CDataWrite(0x1081,0x00);
    I2CDataWrite(0x1082,0x00);

    /* enable DMA checksum and reset checksum */
    I2CDataWrite(0x4280,0x01);
    I2CDataWrite(0x4284,0x00);
    I2CDataWrite(0x4285,0x00);
    I2CDataWrite(0x4164,0x00);

    size = pages * pageSize;
    for(i=0;i<size;i++)
    {
	if((i>=rsvSec2) || (i <rsvSec1))
	{
		chk1 += *(pbuf+i);
	}
	if(chk1>=0x10000)
	{
		chk1 -= 0x10000;
	}
   }

    while( pages ) {
   	 if((pages%0x40)==0)
   	 {
		printk("page:0x%x",pages);
    	}
   	 if((addr>=rsvSec1) && (addr <rsvSec2) && (usage!=0))
   	 {
		addr += 0x1000;
               		pbuf += 0x1000;
		pages -= 0x10;
		continue;
    	}
    	if((pages==1))
   	 {
		for (i = 0; i < pageSize ; i++) {
            		printk("%2x ",*(pbuf+i));
		if((i%0x10)==0x0f) printk("\n");
    		}
    	}

    	dmemBank = pages % 2;
    	I2CDataWrite(0x1081,dmemBank*0x20);
    	I2CDataWrite(0x1084,(1<<dmemBank));
    	I2C_7002DmemWr(dmemBank,pageSize,pbuf);		
		
     	I2C_SPIFlashWrEnable();
     	I2CDataWrite(0x40e7,0x00);
     	I2C_SPIFlashPortWrite(SPI_CMD_BYTE_PROG);               /* Write one byte command*/
     	I2C_SPIFlashPortWrite((UINT8)(addr >> 16));               /* Send 3 bytes address*/
     	I2C_SPIFlashPortWrite((UINT8)(addr >> 8));
     	I2C_SPIFlashPortWrite((UINT8)(addr));

    	I2CDataWrite(0x4160,0x01);
	count = 30;
	pbuf += pageSize;
	addr += pageSize;
        	pages --;
	while( hsI2CDataRead(0x4003) == 0 )
	{
		count--;
		tmrUsWait(200);/* wait for DMA done */
		if( count == 0 )
		{
			printk("DMA time out: %2x, 0x%2x%2x, %2x\n",pages,
				hsI2CDataRead(0x4179),hsI2CDataRead(0x4178),hsI2CDataRead(0x40E6));
			hsI2CDataWrite(0x4011,0x10);
			hsI2CDataWrite(0x1010,0x02);
			pbuf -= pageSize;
        			addr -= pageSize;
        			pages ++;
			hsI2CDataWrite(0x1010,0x00);
			break;
		}
	}
	hsI2CDataWrite(0x4003, 0x02);
    	I2CDataWrite(0x40e7,0x01);
    }
	
    tmrUsWait(500);/* wait for DMA done */

    temp = hsI2CDataRead(0x4285);
    chk2 = hsI2CDataRead(0x4284);
    chk2 = chk2 | (temp<<8);
    printk("checksum: 0x%x 0x%x\n",chk1,chk2);
    
    return err;
}

UINT32 I2C_7002DmemRd(
	UINT32 bankNum,
	UINT32 byteNum,
	UINT8* pbuf
)
{
	UINT32 i, bank;

	bank = 0x40+bankNum;
	hsI2CDataWrite(0x10A6,bank);

	//for(i=0;i<byteNum;i+=4)
		
	for(i=0;i<byteNum;i++)
	{
		seqI2CDataRead((0x1800+i),(pbuf+i));
	}
	
	bank = 0x40 + ((bankNum+1)%2);
	hsI2CDataWrite(0x10A6,bank);
}

UINT32 I2C_SPIFlashRead_DMA(
	UINT32 addr,
	UINT32 size,
	UINT8 *pbuf
)
{
    UINT8* pbufR;
    UINT32 ch, err = SUCCESS, dmemBank;
    UINT32 i, ret, count=0, bytes, offset, tempSize;
    UINT32 pageSize = 0x100;


	/* Set DMA bytecnt as 256-1 */
	hsI2CDataWrite(0x4170,0xff);
	hsI2CDataWrite(0x4171,0x00);
	hsI2CDataWrite(0x4172,0x00);

	/* Set DMA bank & DMA start address */
	hsI2CDataWrite(0x1084,0x01);
	hsI2CDataWrite(0x1080,0x00);
	hsI2CDataWrite(0x1081,0x00);
	hsI2CDataWrite(0x1082,0x00);

	/* enable DMA checksum and reset checksum */
	hsI2CDataWrite(0x4280,0x01);
	hsI2CDataWrite(0x4284,0x00);
	hsI2CDataWrite(0x4285,0x00);
	hsI2CDataWrite(0x4164,0x01);

	while(size)
	{
		if( size > pageSize )
		{
			tempSize = 0x100;
			size -= tempSize;
		}
		else
		{
			tempSize = size;
			size = 0;
		}
		
    	hsI2CDataWrite(0x40e7,0x00);
    	I2C_SPIFlashPortWrite(SPI_CMD_BYTE_READ);               /* Write one byte command*/
    	I2C_SPIFlashPortWrite((UINT8)(addr >> 16));               /* Send 3 bytes address*/
    	I2C_SPIFlashPortWrite((UINT8)(addr >> 8));
    	I2C_SPIFlashPortWrite((UINT8)(addr));

		if( ((size/0x100)%0x40)==0x00 )
		{
            printk("RE:0x%x\n",((size/0x100)%0x40));
		}
		dmemBank = count % 2;
		hsI2CDataWrite(0x1081,dmemBank*0x20);
		hsI2CDataWrite(0x1084,(1<<dmemBank));
    	hsI2CDataWrite(0x4160,0x01);
		tmrUsWait(100);
    	hsI2CDataWrite(0x40e7,0x01);
		I2C_7002DmemRd(dmemBank,tempSize,pbuf);

		pbuf += pageSize;
		addr += pageSize;
		count++;
	}

    return err;
}


/*************************************************************************************************** 
	BB_I2CCalibResModify: used for replace calibration data 
	resId: resource ID (0: 3ACALI.BIN, 1:LSC.BIN, 2:LSC_DQ.BIN 
	resBuf: buffer of data which will be used to replace original calibration data in serial flash
****************************************************************************************************/
UINT32
BB_I2CCalibResModify(
	UINT32 resId,
	UINT8* resBuf
)
{
	UINT32 i;
	UINT32 spiId, spiType;
	UINT32 spiSize, sectorSize = 0x1000, pageSize = 0x100;
	UINT32 resStartAddr, resEndAddr;

	UINT32 resSize, residue;
	
#if 1 /* for 13M sensor IMX091 */
	UINT32 resOffset = 0x7000;
	UINT32 res3acaliSize = 528;
	UINT32 resLscSize = ((3560+15)>>4)<<4;
	UINT32 resLscDqSize = ((13276+15)>>4)<<4;
#else /* for 8M sensor IMX175 */
	UINT32 resOffset = 0x5000;
	UINT32 res3acaliSize = 528;
	UINT32 resLscSize = ((2292+15)>>4)<<4;
	UINT32 resLscDqSize = ((8521+15)>>4)<<4;
#endif

	UINT32 startSector, finalSector, sectorCount;

	UINT8* pBuf;
	
 	if( resId > 0x02 )
 	{
 		printk(" ID ERROR \n");
 		return FAIL;
 	}

	I2C_SPIInit();

	spiId = I2C_SPIFlashReadId();
	if( spiId == 0 )
	{
		printk("read id failed\n");
	 	return FAIL;
	}
	/*printk("spiSize:0x%x\n",&spiSize);*/
	spiType = BB_SerialFlashTypeCheck(spiId, &spiSize);
	
	if( resId == 0x00 )/* 3ACALI.BIN */
	{
		resStartAddr = spiSize - resOffset;
		resEndAddr = resStartAddr + res3acaliSize;
		resSize = res3acaliSize;
	}
	else if( resId == 0x01 )/* LSC.BIN */
	{
		resStartAddr = spiSize - resOffset + res3acaliSize;
		resEndAddr = resStartAddr + resLscSize;
		resSize = resLscSize;
	}
	else /* LSC_DQ.BIN */
	{
		resStartAddr = spiSize -resOffset + res3acaliSize + resLscSize;
		resEndAddr = resStartAddr + resLscDqSize;
		resSize = resLscDqSize;
	}
	printk("%x %x %x\n", resStartAddr, resEndAddr, resSize);

	startSector = (resStartAddr/sectorSize)*sectorSize;
	finalSector = (resEndAddr/sectorSize)*sectorSize;
	sectorCount = ((finalSector - startSector)/sectorSize) + 1;
	residue = resStartAddr - startSector;

	pBuf = osMemAlloc( sectorCount * sectorSize );
	printk("%x %x %x %x\n", startSector, finalSector, sectorCount, residue);
	
	I2C_SPIFlashRead_DMA(startSector,(sectorCount*sectorSize),pBuf);

	memcpy((pBuf+residue), resBuf, resSize);

	if( spiType == 2 )
	{
		for( i=0; i<sectorCount; i++ )
		{
			I2C_SPISectorErase((startSector+(i*sectorSize)),0);
		}
	}
	else if( spiType == 1 || spiType == 3 )
	{
		for( i=0; i<sectorCount; i++ )
		{
			I2C_SPISectorErase((startSector+(i*sectorSize)),1);
		}
	}
	tmrUsWait(0x100);
	printk("pBuf:%x %x\n",pBuf,resBuf);
	I2C_SPIFlashWrite_DMA( startSector/pageSize, (sectorCount * sectorSize)/pageSize, 0, pBuf );

	osMemFree( pBuf );

	return SUCCESS;
}

/*-------------------------------------------------------------------------------------------------- 
	BB_I2CCalibResRead: used for read calibration data 
	resId: resource ID (0: 3ACALI.BIN, 1:LSC.BIN, 2:LSC_DQ.BIN )
	size: return resource size
	bufAddr: return buffer address
---------------------------------------------------------------------------------------------------*/
UINT32
BB_I2CCalibResRead(
	UINT32 resId,
	UINT32* size,
	UINT32* bufAddr
)
{
	UINT32 i;
	UINT32 spiId, spiType;
	UINT32 spiSize, sectorSize = 0x1000, pageSize = 0x100;
	UINT32 resStartAddr, resEndAddr;

	UINT32 resSize, residue;
#if 1 /* for 13M sensor IMX091 */
	UINT32 resOffset = 0x7000;
	UINT32 res3acaliSize = 520;
	UINT32 resLscSize = 3560; 
	UINT32 resLscDqSize = 13276; 
#else /* for 8M sensor IMX175 */
	UINT32 resOffset = 0x5000;
	UINT32 res3acaliSize = 520;
	UINT32 resLscSize = 2292; 
	UINT32 resLscDqSize = 8521; 
#endif

	UINT8* pBuf;

	UINT32 startSector, finalSector, sectorCount;
	
 	if( resId > 0x02 )
 	{
 		printk(" ID ERROR \n");
 		return FAIL;
 	}

	I2C_SPIInit();

	spiId = I2C_SPIFlashReadId();
	if( spiId == 0 )
	{
		printk("read id failed\n");
	 	return FAIL;
	}
	/*printk("spiSize:0x%x\n",&spiSize);*/
	spiType = BB_SerialFlashTypeCheck(spiId, &spiSize);
	
	if( resId == 0x00 )/* 3ACALI.BIN */
	{
		resStartAddr = spiSize - resOffset;
		resSize = res3acaliSize;
	}
	else if( resId == 0x01 )/* LSC.BIN */
	{
		resStartAddr = spiSize - resOffset + (((res3acaliSize+0x0f)>>4)<<4);
		resSize = resLscSize;
	}
	else /* LSC_DQ.BIN */
	{
		resStartAddr = spiSize -resOffset + (((res3acaliSize+0x0f)>>4)<<4) 
										  + (((resLscSize+0x0f)>>4)<<4);
		resSize = resLscDqSize;
	}
	/*printk("%x %x\n", resStartAddr, resSize);*/

	/* buffer used to stored calibration data */
	pBuf = osMemAlloc( resSize );
	printk("pBuf resSize:%x %x\n", pBuf, resSize);
	
	I2C_SPIFlashRead_DMA(resStartAddr,resSize,pBuf);
	*bufAddr = pBuf;
	printk("pBuf resSize:%x %x\n", (*bufAddr), resSize);

	*size = resSize;

	return SUCCESS;
}
