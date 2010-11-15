#include <linux/slab.h>
#include "usb.h"
#include "scsiglue.h"
#include "smcommon.h"
#include "smil.h"

int         Check_D_LogCHS              (WORD *,BYTE *,BYTE *);
void        Initialize_D_Media          (void);
void        PowerOff_D_Media            (void);
int         Check_D_MediaPower          (void);
int         Check_D_MediaExist          (void);
int         Check_D_MediaWP             (void);
int         Check_D_MediaFmt            (struct us_data *);
int         Check_D_MediaFmtForEraseAll (struct us_data *);
int         Conv_D_MediaAddr            (struct us_data *, DWORD);
int         Inc_D_MediaAddr             (struct us_data *);
int         Check_D_FirstSect           (void);
int         Check_D_LastSect            (void);
int         Media_D_ReadOneSect         (struct us_data *, WORD, BYTE *);
int         Media_D_WriteOneSect        (struct us_data *, WORD, BYTE *);
int         Media_D_CopyBlockHead       (struct us_data *);
int         Media_D_CopyBlockTail       (struct us_data *);
int         Media_D_EraseOneBlock       (void);
int         Media_D_EraseAllBlock       (void);

int  Copy_D_BlockAll             (struct us_data *, DWORD);
int  Copy_D_BlockHead            (struct us_data *);
int  Copy_D_BlockTail            (struct us_data *);
int  Reassign_D_BlockHead        (struct us_data *);

int  Assign_D_WriteBlock         (void);
int  Release_D_ReadBlock         (struct us_data *);
int  Release_D_WriteBlock        (struct us_data *);
int  Release_D_CopySector        (struct us_data *);

int  Copy_D_PhyOneSect           (struct us_data *);
int  Read_D_PhyOneSect           (struct us_data *, WORD, BYTE *);
int  Write_D_PhyOneSect          (struct us_data *, WORD, BYTE *);
int  Erase_D_PhyOneBlock         (struct us_data *);

int  Set_D_PhyFmtValue           (struct us_data *);
int  Search_D_CIS                (struct us_data *);
int  Make_D_LogTable             (struct us_data *);
void Check_D_BlockIsFull         (void);

int  MarkFail_D_PhyOneBlock      (struct us_data *);

DWORD ErrXDCode;
DWORD ErrCode;
//BYTE  SectBuf[SECTSIZE];
BYTE  WorkBuf[SECTSIZE];
BYTE  Redundant[REDTSIZE];
BYTE  WorkRedund[REDTSIZE];
//WORD  Log2Phy[MAX_ZONENUM][MAX_LOGBLOCK];
WORD  *Log2Phy[MAX_ZONENUM];                 // 128 x 1000,   Log2Phy[MAX_ZONENUM][MAX_LOGBLOCK];
BYTE  Assign[MAX_ZONENUM][MAX_BLOCKNUM/8];
WORD  AssignStart[MAX_ZONENUM];
WORD  ReadBlock;
WORD  WriteBlock;
DWORD MediaChange;
DWORD SectCopyMode;

extern struct SSFDCTYPE  Ssfdc;
extern struct ADDRESS    Media;
extern struct CIS_AREA   CisArea;

//BIT Controll Macro
BYTE BitData[] = { 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80 } ;
#define Set_D_Bit(a,b)    (a[(BYTE)((b)/8)]|= BitData[(b)%8])
#define Clr_D_Bit(a,b)    (a[(BYTE)((b)/8)]&=~BitData[(b)%8])
#define Chk_D_Bit(a,b)    (a[(BYTE)((b)/8)] & BitData[(b)%8])

//extern PBYTE    SMHostAddr;
extern BYTE     IsSSFDCCompliance;
extern BYTE     IsXDCompliance;


//
////Power Controll & Media Exist Check Function
////----- Init_D_SmartMedia() --------------------------------------------
//int Init_D_SmartMedia(void)
//{
//    int     i;
//
//    EMCR_Print("Init_D_SmartMedia start\n");
//    for (i=0; i<MAX_ZONENUM; i++)
//    {
//        if (Log2Phy[i]!=NULL)
//        {
//            EMCR_Print("ExFreePool Zone = %x, Addr = %x\n", i, Log2Phy[i]);
//            ExFreePool(Log2Phy[i]);
//            Log2Phy[i] = NULL;
//        }
//    }
//
//    Initialize_D_Media();
//    return(NO_ERROR);
//}

//----- SM_FreeMem() -------------------------------------------------
int SM_FreeMem(void)
{
	int	i;

	printk("SM_FreeMem start\n");
	for (i=0; i<MAX_ZONENUM; i++)
	{
		if (Log2Phy[i]!=NULL)
		{
			printk("Free Zone = %x, Addr = %p\n", i, Log2Phy[i]);
			kfree(Log2Phy[i]);
			Log2Phy[i] = NULL;
		}
	}
	return(NO_ERROR);
}

////----- Pwoff_D_SmartMedia() -------------------------------------------
//int Pwoff_D_SmartMedia(void)
//{
//    PowerOff_D_Media();
//    return(NO_ERROR);
//}
//
////----- Check_D_SmartMedia() -------------------------------------------
//int Check_D_SmartMedia(void)
//{
//    if (Check_D_MediaExist())
//        return(ErrCode);
//
//    return(NO_ERROR);
//}
//
////----- Check_D_Parameter() --------------------------------------------
//int Check_D_Parameter(PFDO_DEVICE_EXTENSION fdoExt,WORD *pcyl,BYTE *phead,BYTE *psect)
//{
//    if (Check_D_MediaPower())
//        return(ErrCode);
//
//    if (Check_D_MediaFmt(fdoExt))
//        return(ErrCode);
//
//    if (Check_D_LogCHS(pcyl,phead,psect))
//        return(ErrCode);
//
//    return(NO_ERROR);
//}

//SmartMedia Read/Write/Erase Function
//----- Media_D_ReadSector() -------------------------------------------
int Media_D_ReadSector(struct us_data *us, DWORD start,WORD count,BYTE *buf)
{
	WORD len, bn;

	//if (Check_D_MediaPower())        ; 在 6250 don't care
	//    return(ErrCode);
	//if (Check_D_MediaFmt(fdoExt))    ;
	//    return(ErrCode);
	if (Conv_D_MediaAddr(us, start))
		return(ErrCode);

	while(1)
	{
		len = Ssfdc.MaxSectors - Media.Sector;
		if (count > len)
			bn = len;
		else
			bn = count;
		//if (Media_D_ReadOneSect(fdoExt, SectBuf))
		//if (Media_D_ReadOneSect(fdoExt, count, buf))
		if (Media_D_ReadOneSect(us, bn, buf))
		{
			ErrCode = ERR_EccReadErr;
			return(ErrCode);
		}

		Media.Sector += bn;
		count -= bn;

		if (count<=0)
			break;

		buf += bn * SECTSIZE;

		if (Inc_D_MediaAddr(us))
			return(ErrCode);
	}

	return(NO_ERROR);
}
// here
//----- Media_D_CopySector() ------------------------------------------
int Media_D_CopySector(struct us_data *us, DWORD start,WORD count,BYTE *buf)
{
	//DWORD mode;
	//int i;
	WORD len, bn;
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	//printk("Media_D_CopySector !!!\n");
	if (Conv_D_MediaAddr(us, start))
		return(ErrCode);

	while(1)
	{
		if (Assign_D_WriteBlock())
			return(ERROR);

		len = Ssfdc.MaxSectors - Media.Sector;
		if (count > len)
			bn = len;
		else
		bn = count;

		//if (Ssfdc_D_CopyBlock(fdoExt,count,buf,Redundant))
		if (Ssfdc_D_CopyBlock(us,bn,buf,Redundant))
		{
			ErrCode = ERR_WriteFault;
			return(ErrCode);
		}

		Media.Sector = 0x1F;
		//if (Release_D_ReadBlock(fdoExt))
		if (Release_D_CopySector(us))
		{
			if (ErrCode==ERR_HwError)
			{
				ErrCode = ERR_WriteFault;
				return(ErrCode);
			}
		}
		count -= bn;

		if (count<=0)
			break;

		buf += bn * SECTSIZE;

		if (Inc_D_MediaAddr(us))
			return(ErrCode);

	}
	return(NO_ERROR);
}

//----- Release_D_CopySector() ------------------------------------------
int Release_D_CopySector(struct us_data *us)
{
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	Log2Phy[Media.Zone][Media.LogBlock]=WriteBlock;
	Media.PhyBlock=ReadBlock;

	if (Media.PhyBlock==NO_ASSIGN)
	{
		Media.PhyBlock=WriteBlock;
		return(SUCCESS);
	}

	Clr_D_Bit(Assign[Media.Zone],Media.PhyBlock);
	Media.PhyBlock=WriteBlock;

	return(SUCCESS);
}
/*
//----- Media_D_WriteSector() ------------------------------------------
int Media_D_WriteSector(PFDO_DEVICE_EXTENSION fdoExt, DWORD start,WORD count,BYTE *buf)
{
    int i;
    WORD len, bn;
    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
    ADDRESS_T   bb = (ADDRESS_T) &Media;

    //if (Check_D_MediaPower())
    //    return(ErrCode);
    //
    //if (Check_D_MediaFmt(fdoExt))
    //    return(ErrCode);
    //
    //if (Check_D_MediaWP())
    //    return(ErrCode);

    if (Conv_D_MediaAddr(fdoExt, start))
        return(ErrCode);

    //ENE_Print("Media_D_WriteSector --- Sector = %x\n", Media.Sector);
    if (Check_D_FirstSect())
    {
        if (Media_D_CopyBlockHead(fdoExt))
        {
            ErrCode = ERR_WriteFault;
            return(ErrCode);
        }
    }

    while(1)
    {
        if (!Check_D_FirstSect())
        {
            if (Assign_D_WriteBlock())
                return(ErrCode);
        }

        len = Ssfdc.MaxSectors - Media.Sector;
        if (count > len)
           bn = len;
        else
           bn = count;
        //for(i=0;i<SECTSIZE;i++)
        //    SectBuf[i]=*buf++;

        //if (Media_D_WriteOneSect(fdoExt, SectBuf))
        if (Media_D_WriteOneSect(fdoExt, bn, buf))
        {
            ErrCode = ERR_WriteFault;
            return(ErrCode);
        }

        Media.Sector += bn - 1;

        if (!Check_D_LastSect())
        {
            if (Release_D_ReadBlock(fdoExt))

            {    if (ErrCode==ERR_HwError)
                {
                    ErrCode = ERR_WriteFault;
                    return(ErrCode);
                }
            }
        }

        count -= bn;

        if (count<=0)
            break;

        buf += bn * SECTSIZE;

        //if (--count<=0)
        //    break;

        if (Inc_D_MediaAddr(fdoExt))
            return(ErrCode);
    }

    if (!Check_D_LastSect())
        return(NO_ERROR);

    if (Inc_D_MediaAddr(fdoExt))
        return(ErrCode);

    if (Media_D_CopyBlockTail(fdoExt))
    {
        ErrCode = ERR_WriteFault;
        return(ErrCode);
    }

    return(NO_ERROR);
}
//
////----- Media_D_EraseBlock() -------------------------------------------
//int Media_D_EraseBlock(PFDO_DEVICE_EXTENSION fdoExt, DWORD start,WORD count)
//{
//    if (Check_D_MediaPower())
//        return(ErrCode);
//
//    if (Check_D_MediaFmt(fdoExt))
//        return(ErrCode);
//
//    if (Check_D_MediaWP())
//        return(ErrCode);
//
//    if (Conv_D_MediaAddr(start))
//        return(ErrCode);
//
//    while(Check_D_FirstSect()) {
//        if (Inc_D_MediaAddr(fdoExt))
//            return(ErrCode);
//
//        if (--count<=0)
//            return(NO_ERROR);
//    }
//
//    while(1) {
//        if (!Check_D_LastSect())
//            if (Media_D_EraseOneBlock())
//                if (ErrCode==ERR_HwError)
//                {
//                    ErrCode = ERR_WriteFault;
//                    return(ErrCode);
//                }
//
//        if (Inc_D_MediaAddr(fdoExt))
//            return(ErrCode);
//
//        if (--count<=0)
//            return(NO_ERROR);
//    }
//}
//
////----- Media_D_EraseAll() ---------------------------------------------
//int Media_D_EraseAll(PFDO_DEVICE_EXTENSION fdoExt)
//{
//    if (Check_D_MediaPower())
//        return(ErrCode);
//
//    if (Check_D_MediaFmtForEraseAll(fdoExt))
//        return(ErrCode);
//
//    if (Check_D_MediaWP())
//        return(ErrCode);
//
//    if (Media_D_EraseAllBlock())
//        return(ErrCode);
//
//    return(NO_ERROR);
//}

//SmartMedia Write Function for One Sector Write Mode
//----- Media_D_OneSectWriteStart() ------------------------------------
int Media_D_OneSectWriteStart(PFDO_DEVICE_EXTENSION fdoExt,DWORD start,BYTE *buf)
{
//  int i;
//  SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
//  ADDRESS_T   bb = (ADDRESS_T) &Media;
//
//  //if (Check_D_MediaPower())
//  //    return(ErrCode);
//  //if (Check_D_MediaFmt(fdoExt))
//  //    return(ErrCode);
//  //if (Check_D_MediaWP())
//  //    return(ErrCode);
//  if (Conv_D_MediaAddr(fdoExt, start))
//      return(ErrCode);
//
//  if (Check_D_FirstSect())
//      if (Media_D_CopyBlockHead(fdoExt))
//      {
//          ErrCode = ERR_WriteFault;
//          return(ErrCode);
//      }
//
//  if (!Check_D_FirstSect())
//      if (Assign_D_WriteBlock())
//          return(ErrCode);
//
//  //for(i=0;i<SECTSIZE;i++)
//  //    SectBuf[i]=*buf++;
//
//  //if (Media_D_WriteOneSect(fdoExt, SectBuf))
//  if (Media_D_WriteOneSect(fdoExt, buf))
//  {
//      ErrCode = ERR_WriteFault;
//      return(ErrCode);
//  }
//
//  if (!Check_D_LastSect())
//  {
//      if (Release_D_ReadBlock(fdoExt))
//          if (ErrCode==ERR_HwError)
//          {
//              ErrCode = ERR_WriteFault;
//              return(ErrCode);
//          }
//  }

    return(NO_ERROR);
}

//----- Media_D_OneSectWriteNext() -------------------------------------
int Media_D_OneSectWriteNext(PFDO_DEVICE_EXTENSION fdoExt, BYTE *buf)
{
//  int i;
//  SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
//  ADDRESS_T   bb = (ADDRESS_T) &Media;
//
//  if (Inc_D_MediaAddr(fdoExt))
//      return(ErrCode);
//
//  if (!Check_D_FirstSect())
//    if (Assign_D_WriteBlock())
//      return(ErrCode);
//
//  //for(i=0;i<SECTSIZE;i++)
//  //    SectBuf[i]=*buf++;
//
//  //if (Media_D_WriteOneSect(fdoExt, SectBuf))
//  if (Media_D_WriteOneSect(fdoExt, buf))
//  {
//      ErrCode = ERR_WriteFault;
//      return(ErrCode);
//  }
//
//  if (!Check_D_LastSect())
//  {
//      if (Release_D_ReadBlock(fdoExt))
//          if (ErrCode==ERR_HwError)
//          {
//              ErrCode = ERR_WriteFault;
//              return(ErrCode);
//          }
//  }

    return(NO_ERROR);
}

//----- Media_D_OneSectWriteFlush() ------------------------------------
int Media_D_OneSectWriteFlush(PFDO_DEVICE_EXTENSION fdoExt)
{
    if (!Check_D_LastSect())
        return(NO_ERROR);

    if (Inc_D_MediaAddr(fdoExt))
        return(ErrCode);

    if (Media_D_CopyBlockTail(fdoExt))
    {
        ErrCode = ERR_WriteFault;
        return(ErrCode);
    }

    return(NO_ERROR);
}
//
////LED Tern On/Off Subroutine
////----- SM_EnableLED() -----------------------------------------------
//void SM_EnableLED(PFDO_DEVICE_EXTENSION fdoExt, BOOLEAN enable)
//{
//    if (fdoExt->Drive_IsSWLED)
//    {
//        if (enable)
//           Led_D_TernOn();
//        else
//           Led_D_TernOff();
//    }
//}
//
////----- Led_D_TernOn() -------------------------------------------------
//void Led_D_TernOn(void)
//{
//    if (Check_D_CardStsChg())
//        MediaChange=ERROR;
//
//    Cnt_D_LedOn();
//}
//
////----- Led_D_TernOff() ------------------------------------------------
//void Led_D_TernOff(void)
//{
//    if (Check_D_CardStsChg())
//        MediaChange=ERROR;
//
//    Cnt_D_LedOff();
//}
//
////SmartMedia Logical Format Subroutine
////----- Check_D_LogCHS() -----------------------------------------------
//int Check_D_LogCHS(WORD *c,BYTE *h,BYTE *s)
//{
//    switch(Ssfdc.Model) {
//        case SSFDC1MB:   *c=125; *h= 4; *s= 4; break;
//        case SSFDC2MB:   *c=125; *h= 4; *s= 8; break;
//        case SSFDC4MB:   *c=250; *h= 4; *s= 8; break;
//        case SSFDC8MB:   *c=250; *h= 4; *s=16; break;
//        case SSFDC16MB:  *c=500; *h= 4; *s=16; break;
//        case SSFDC32MB:  *c=500; *h= 8; *s=16; break;
//        case SSFDC64MB:  *c=500; *h= 8; *s=32; break;
//        case SSFDC128MB: *c=500; *h=16; *s=32; break;
//        default:         *c= 0;  *h= 0; *s= 0; ErrCode = ERR_NoSmartMedia;    return(ERROR);
//    }
//
//    return(SUCCESS);
//}
//
////Power Controll & Media Exist Check Subroutine
////----- Initialize_D_Media() -------------------------------------------
//void Initialize_D_Media(void)
//{
//    ErrCode      = NO_ERROR;
//    MediaChange  = ERROR;
//    SectCopyMode = COMPLETED;
//    Cnt_D_Reset();
//}
//
////----- PowerOff_D_Media() ---------------------------------------------
//void PowerOff_D_Media(void)
//{
//    Cnt_D_PowerOff();
//}
//
////----- Check_D_MediaPower() -------------------------------------------
//int Check_D_MediaPower(void)
//{
//    //usleep(56*1024);
//    if (Check_D_CardStsChg())
//        MediaChange = ERROR;
//    //usleep(56*1024);
//    if ((!Check_D_CntPower())&&(!MediaChange))  // 有 power & Media 沒被 change, 則 return success
//        return(SUCCESS);
//    //usleep(56*1024);
//
//    if (Check_D_CardExist())                    // Check if card is not exist, return err
//    {
//        ErrCode        = ERR_NoSmartMedia;
//        MediaChange = ERROR;
//        return(ERROR);
//    }
//    //usleep(56*1024);
//    if (Cnt_D_PowerOn())
//    {
//        ErrCode        = ERR_NoSmartMedia;
//        MediaChange = ERROR;
//        return(ERROR);
//    }
//    //usleep(56*1024);
//    Ssfdc_D_Reset(fdoExt);
//    //usleep(56*1024);
//    return(SUCCESS);
//}
//
////-----Check_D_MediaExist() --------------------------------------------
//int Check_D_MediaExist(void)
//{
//    if (Check_D_CardStsChg())
//        MediaChange = ERROR;
//
//    if (!Check_D_CardExist())
//    {
//        if (!MediaChange)
//            return(SUCCESS);
//
//        ErrCode = ERR_ChangedMedia;
//        return(ERROR);
//    }
//
//    ErrCode = ERR_NoSmartMedia;
//
//    return(ERROR);
//}
//
////----- Check_D_MediaWP() ----------------------------------------------
//int Check_D_MediaWP(void)
//{
//    if (Ssfdc.Attribute &MWP)
//    {
//        ErrCode = ERR_WrtProtect;
//        return(ERROR);
//    }
//
//    return(SUCCESS);
//}
*/
//SmartMedia Physical Format Test Subroutine
//----- Check_D_MediaFmt() ---------------------------------------------
int Check_D_MediaFmt(struct us_data *us)
{
	printk("Check_D_MediaFmt\n");
	//ULONG i,j, result=FALSE, zone,block;

	//usleep(56*1024);
	if (!MediaChange)
		return(SUCCESS);

	MediaChange  = ERROR;
	SectCopyMode = COMPLETED;

	//usleep(56*1024);
	if (Set_D_PhyFmtValue(us))
	{
		ErrCode = ERR_UnknownMedia;
		return(ERROR);
	}
	
	//usleep(56*1024);
	if (Search_D_CIS(us))
	{
		ErrCode = ERR_IllegalFmt;
		return(ERROR);
	}


    MediaChange = SUCCESS;
    return(SUCCESS);
}
/*
////----- Check_D_BlockIsFull() ----------------------------------
//void Check_D_BlockIsFull()
//{
//    ULONG i, block;
//
//    if (IsXDCompliance || IsSSFDCCompliance)
//    {
//       // If the blocks are full then return write-protect.
//       block = Ssfdc.MaxBlocks/8;
//       for (Media.Zone=0; Media.Zone<Ssfdc.MaxZones; Media.Zone++)
//       {
//           if (Log2Phy[Media.Zone]==NULL)
//           {
//               if (Make_D_LogTable())
//               {
//                   ErrCode = ERR_IllegalFmt;
//                   return;
//               }
//           }
//
//           for (i=0; i<block; i++)
//           {
//               if (Assign[Media.Zone][i] != 0xFF)
//                  return;
//           }
//       }
//       Ssfdc.Attribute |= WP;
//    }
//}
//
//
////----- Check_D_MediaFmtForEraseAll() ----------------------------------
//int Check_D_MediaFmtForEraseAll(PFDO_DEVICE_EXTENSION fdoExt)
//{
//    MediaChange  = ERROR;
//    SectCopyMode = COMPLETED;
//
//    if (Set_D_PhyFmtValue(fdoExt))
//    {
//        ErrCode = ERR_UnknownMedia;
//        return(ERROR);
//    }
//
//    if (Search_D_CIS(fdoExt))
//    {
//        ErrCode = ERR_IllegalFmt;
//        return(ERROR);
//    }
//
//    return(SUCCESS);
//}
*/
//SmartMedia Physical Address Controll Subroutine
//----- Conv_D_MediaAddr() ---------------------------------------------
int Conv_D_MediaAddr(struct us_data *us, DWORD addr)
{
	DWORD temp;
	//ULONG  zz;
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	temp           = addr/Ssfdc.MaxSectors;
	Media.Zone     = (BYTE) (temp/Ssfdc.MaxLogBlocks);

	if (Log2Phy[Media.Zone]==NULL)
	{
		if (Make_D_LogTable(us))
		{
			ErrCode = ERR_IllegalFmt;
			return(ERROR);
		}
	}

	Media.Sector   = (BYTE) (addr%Ssfdc.MaxSectors);
	Media.LogBlock = (WORD) (temp%Ssfdc.MaxLogBlocks);

	if (Media.Zone<Ssfdc.MaxZones)
	{
		Clr_D_RedundantData(Redundant);
		Set_D_LogBlockAddr(Redundant);
		Media.PhyBlock = Log2Phy[Media.Zone][Media.LogBlock];
		return(SUCCESS);
	}

	ErrCode = ERR_OutOfLBA;
	return(ERROR);
}

//----- Inc_D_MediaAddr() ----------------------------------------------
int Inc_D_MediaAddr(struct us_data *us)
{
	WORD        LogBlock = Media.LogBlock;
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	if (++Media.Sector<Ssfdc.MaxSectors)
		return(SUCCESS);

	if (Log2Phy[Media.Zone]==NULL)
	{
		if (Make_D_LogTable(us))
		{
			ErrCode = ERR_IllegalFmt;
			return(ERROR);
		}
	}

	Media.Sector=0;
	Media.LogBlock = LogBlock;

	if (++Media.LogBlock<Ssfdc.MaxLogBlocks)
	{
		Clr_D_RedundantData(Redundant);
		Set_D_LogBlockAddr(Redundant);
		Media.PhyBlock=Log2Phy[Media.Zone][Media.LogBlock];
		return(SUCCESS);
	}

	Media.LogBlock=0;

	if (++Media.Zone<Ssfdc.MaxZones)
	{
		if (Log2Phy[Media.Zone]==NULL)
		{
			if (Make_D_LogTable(us))
			{
				ErrCode = ERR_IllegalFmt;
				return(ERROR);
			}
		}

		Media.LogBlock = 0;

		Clr_D_RedundantData(Redundant);
		Set_D_LogBlockAddr(Redundant);
		Media.PhyBlock=Log2Phy[Media.Zone][Media.LogBlock];
		return(SUCCESS);
	}

	Media.Zone=0;
	ErrCode = ERR_OutOfLBA;

	return(ERROR);
}
/*
//----- Check_D_FirstSect() --------------------------------------------
int Check_D_FirstSect(void)
{
    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
    ADDRESS_T   bb = (ADDRESS_T) &Media;

    if (!Media.Sector)
        return(SUCCESS);

    return(ERROR);
}

//----- Check_D_LastSect() ---------------------------------------------
int Check_D_LastSect(void)
{
    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
    ADDRESS_T   bb = (ADDRESS_T) &Media;

    if (Media.Sector<(Ssfdc.MaxSectors-1))
        return(ERROR);

    return(SUCCESS);
}
*/
//SmartMedia Read/Write Subroutine with Retry
//----- Media_D_ReadOneSect() ------------------------------------------
int Media_D_ReadOneSect(struct us_data *us, WORD count, BYTE *buf)
{
	DWORD err, retry;

	if (!Read_D_PhyOneSect(us, count, buf))
		return(SUCCESS);
	if (ErrCode==ERR_HwError)
		return(ERROR);
	if (ErrCode==ERR_DataStatus)
		return(ERROR);

#ifdef RDERR_REASSIGN
	if (Ssfdc.Attribute &MWP)
	{
		if (ErrCode==ERR_CorReadErr)
			return(SUCCESS);
		return(ERROR);
	}

	err=ErrCode;
	for(retry=0; retry<2; retry++)
	{
		if (Copy_D_BlockAll(us, (err==ERR_EccReadErr)?REQ_FAIL:REQ_ERASE))
		{
			if (ErrCode==ERR_HwError)
				return(ERROR);
			continue;
		}

		ErrCode = err;
		if (ErrCode==ERR_CorReadErr)
			return(SUCCESS);
		return(ERROR);
	}

	MediaChange = ERROR;
#else
	if (ErrCode==ERR_CorReadErr) return(SUCCESS);
#endif

	return(ERROR);
}
/*
//----- Media_D_WriteOneSect() -----------------------------------------
int Media_D_WriteOneSect(PFDO_DEVICE_EXTENSION fdoExt, WORD count, BYTE *buf)
{
    DWORD retry;
    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
    ADDRESS_T   bb = (ADDRESS_T) &Media;

    if (!Write_D_PhyOneSect(fdoExt, count, buf))
        return(SUCCESS);
    if (ErrCode==ERR_HwError)
        return(ERROR);

    for(retry=1; retry<2; retry++)
    {
        if (Reassign_D_BlockHead(fdoExt))
        {
            if (ErrCode==ERR_HwError)
                return(ERROR);
            continue;
        }

        if (!Write_D_PhyOneSect(fdoExt, count, buf))
            return(SUCCESS);
        if (ErrCode==ERR_HwError)
            return(ERROR);
    }

    if (Release_D_WriteBlock(fdoExt))
        return(ERROR);

    ErrCode        = ERR_WriteFault;
    MediaChange = ERROR;
    return(ERROR);
}

//SmartMedia Data Copy Subroutine with Retry
//----- Media_D_CopyBlockHead() ----------------------------------------
int Media_D_CopyBlockHead(PFDO_DEVICE_EXTENSION fdoExt)
{
    DWORD retry;

    for(retry=0; retry<2; retry++)
    {
        if (!Copy_D_BlockHead(fdoExt))
            return(SUCCESS);
        if (ErrCode==ERR_HwError)
            return(ERROR);
    }

    MediaChange = ERROR;
    return(ERROR);
}

//----- Media_D_CopyBlockTail() ----------------------------------------
int Media_D_CopyBlockTail(PFDO_DEVICE_EXTENSION fdoExt)
{
    DWORD retry;

    if (!Copy_D_BlockTail(fdoExt))
        return(SUCCESS);
    if (ErrCode==ERR_HwError)
        return(ERROR);

    for(retry=1; retry<2; retry++)
    {
        if (Reassign_D_BlockHead(fdoExt))
        {
            if (ErrCode==ERR_HwError)
                return(ERROR);
            continue;
        }

        if (!Copy_D_BlockTail(fdoExt))
            return(SUCCESS);
        if (ErrCode==ERR_HwError)
            return(ERROR);
    }

    if (Release_D_WriteBlock(fdoExt))
        return(ERROR);

    ErrCode        = ERR_WriteFault;
    MediaChange = ERROR;
    return(ERROR);
}
//
////----- Media_D_EraseOneBlock() ----------------------------------------
//int Media_D_EraseOneBlock(void)
//{
//    WORD        LogBlock = Media.LogBlock;
//    WORD        PhyBlock = Media.PhyBlock;
//    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
//    ADDRESS_T   bb = (ADDRESS_T) &Media;
//
//    if (Media.PhyBlock==NO_ASSIGN)
//        return(SUCCESS);
//
//    if (Log2Phy[Media.Zone]==NULL)
//    {
//        if (Make_D_LogTable())
//        {
//            ErrCode = ERR_IllegalFmt;
//            return(ERROR);
//        }
//    }
//    Media.LogBlock = LogBlock;
//    Media.PhyBlock = PhyBlock;
//
//    Log2Phy[Media.Zone][Media.LogBlock]=NO_ASSIGN;
//
//    if (Erase_D_PhyOneBlock(fdoExt))
//    {
//        if (ErrCode==ERR_HwError)
//            return(ERROR);
//        if (MarkFail_D_PhyOneBlock())
//            return(ERROR);
//
//        ErrCode = ERR_WriteFault;
//        return(ERROR);
//    }
//
//    Clr_D_Bit(Assign[Media.Zone],Media.PhyBlock);
//    Media.PhyBlock=NO_ASSIGN;
//    return(SUCCESS);
//}
//
////SmartMedia Erase Subroutine
////----- Media_D_EraseAllBlock() ----------------------------------------
//int Media_D_EraseAllBlock(void)
//{
//    WORD cis=0;
//
//    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
//    ADDRESS_T   bb = (ADDRESS_T) &Media;
//
//    MediaChange = ERROR;
//    Media.Sector   = 0;
//
//    for(Media.Zone=0; Media.Zone<Ssfdc.MaxZones; Media.Zone++)
//        for(Media.PhyBlock=0; Media.PhyBlock<Ssfdc.MaxBlocks; Media.PhyBlock++) {
//            if (Ssfdc_D_ReadRedtData(Redundant))
//            {
//                Ssfdc_D_Reset(fdoExt);
//                return(ERROR);
//            }
//
//            Ssfdc_D_Reset(fdoExt);
//            if (!Check_D_FailBlock(Redundant))
//            {
//                if (cis)
//                {
//                    if (Ssfdc_D_EraseBlock(fdoExt))
//                    {
//                        ErrCode = ERR_HwError;
//                        return(ERROR);
//                    }
//
//                    if (Ssfdc_D_CheckStatus())
//                    {
//                        if (MarkFail_D_PhyOneBlock())
//                            return(ERROR);
//                    }
//
//                    continue;
//                }
//
//                if (Media.PhyBlock!=CisArea.PhyBlock)
//                {
//                    ErrCode = ERR_IllegalFmt;
//                    return(ERROR);
//                }
//
//                cis++;
//            }
//
//        }
//    return(SUCCESS);
//}
*/
//SmartMedia Physical Sector Data Copy Subroutine
//----- Copy_D_BlockAll() ----------------------------------------------
int Copy_D_BlockAll(struct us_data *us, DWORD mode)
{
	BYTE sect;
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	sect=Media.Sector;

	if (Assign_D_WriteBlock())
		return(ERROR);
	if (mode==REQ_FAIL)
		SectCopyMode=REQ_FAIL;

	for(Media.Sector=0; Media.Sector<Ssfdc.MaxSectors; Media.Sector++)
	{
		if (Copy_D_PhyOneSect(us))
		{
			if (ErrCode==ERR_HwError)
				return(ERROR);
			if (Release_D_WriteBlock(us))
				return(ERROR);

			ErrCode = ERR_WriteFault;
			Media.PhyBlock=ReadBlock;
			Media.Sector=sect;

			return(ERROR);
		}
	}

	if (Release_D_ReadBlock(us))
		return(ERROR);

	Media.PhyBlock=WriteBlock;
	Media.Sector=sect;
	return(SUCCESS);
}
/*
//----- Copy_D_BlockHead() ---------------------------------------------
int Copy_D_BlockHead(PFDO_DEVICE_EXTENSION fdoExt)
{
    BYTE sect;
    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
    ADDRESS_T   bb = (ADDRESS_T) &Media;

    sect=Media.Sector;
    if (Assign_D_WriteBlock())
        return(ERROR);

    for(Media.Sector=0; Media.Sector<sect; Media.Sector++)
    {
        if (Copy_D_PhyOneSect(fdoExt))
        {
            if (ErrCode==ERR_HwError)
                return(ERROR);
            if (Release_D_WriteBlock(fdoExt))
                return(ERROR);

            ErrCode = ERR_WriteFault;
            Media.PhyBlock=ReadBlock;
            Media.Sector=sect;

            return(ERROR);
        }
    }

    Media.PhyBlock=WriteBlock;
    Media.Sector=sect;
    return(SUCCESS);
}

//----- Copy_D_BlockTail() ---------------------------------------------
int Copy_D_BlockTail(PFDO_DEVICE_EXTENSION fdoExt)
{
    BYTE sect;
    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
    ADDRESS_T   bb = (ADDRESS_T) &Media;

    for(sect=Media.Sector; Media.Sector<Ssfdc.MaxSectors; Media.Sector++)
    {
        if (Copy_D_PhyOneSect(fdoExt))
        {
            if (ErrCode==ERR_HwError)
                return(ERROR);

            Media.PhyBlock=WriteBlock;
            Media.Sector=sect;

            return(ERROR);
        }
    }

    if (Release_D_ReadBlock(fdoExt))
        return(ERROR);

    Media.PhyBlock=WriteBlock;
    Media.Sector=sect;
    return(SUCCESS);
}

//----- Reassign_D_BlockHead() -----------------------------------------
int Reassign_D_BlockHead(PFDO_DEVICE_EXTENSION fdoExt)
{
    DWORD  mode;
    WORD   block;
    BYTE   sect;
    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
    ADDRESS_T   bb = (ADDRESS_T) &Media;

    mode=SectCopyMode;
    block=ReadBlock;
    sect=Media.Sector;

    if (Assign_D_WriteBlock())
        return(ERROR);

    SectCopyMode=REQ_FAIL;

    for(Media.Sector=0; Media.Sector<sect; Media.Sector++)
    {
        if (Copy_D_PhyOneSect(fdoExt))
        {
            if (ErrCode==ERR_HwError)
                return(ERROR);
            if (Release_D_WriteBlock(fdoExt))
                return(ERROR);

            ErrCode = ERR_WriteFault;
            SectCopyMode=mode;
            WriteBlock=ReadBlock;
            ReadBlock=block;
            Media.Sector=sect;
            Media.PhyBlock=WriteBlock;

            return(ERROR);
        }
    }

    if (Release_D_ReadBlock(fdoExt))
        return(ERROR);

    SectCopyMode=mode;
    ReadBlock=block;
    Media.Sector=sect;
    Media.PhyBlock=WriteBlock;
    return(SUCCESS);
}
*/
//SmartMedia Physical Block Assign/Release Subroutine
//----- Assign_D_WriteBlock() ------------------------------------------
int Assign_D_WriteBlock(void)
{
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;
	ReadBlock=Media.PhyBlock;

	for(WriteBlock=AssignStart[Media.Zone]; WriteBlock<Ssfdc.MaxBlocks; WriteBlock++)
	{
		if (!Chk_D_Bit(Assign[Media.Zone],WriteBlock))
		{
			Set_D_Bit(Assign[Media.Zone],WriteBlock);
			AssignStart[Media.Zone]=WriteBlock+1;
			Media.PhyBlock=WriteBlock;
			SectCopyMode=REQ_ERASE;
			//ErrXDCode = NO_ERROR;
			return(SUCCESS);
		}
	}

	for(WriteBlock=0; WriteBlock<AssignStart[Media.Zone]; WriteBlock++)
	{
		if (!Chk_D_Bit(Assign[Media.Zone],WriteBlock))
		{
			Set_D_Bit(Assign[Media.Zone],WriteBlock);
			AssignStart[Media.Zone]=WriteBlock+1;
			Media.PhyBlock=WriteBlock;
			SectCopyMode=REQ_ERASE;
			//ErrXDCode = NO_ERROR;
			return(SUCCESS);
		}
	}

	WriteBlock=NO_ASSIGN;
	ErrCode = ERR_WriteFault;
	// For xD test
	//Ssfdc.Attribute |= WP;
	//ErrXDCode = ERR_WrtProtect;
	return(ERROR);
}

//----- Release_D_ReadBlock() ------------------------------------------
int Release_D_ReadBlock(struct us_data *us)
{
	DWORD mode;
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	mode=SectCopyMode;
	SectCopyMode=COMPLETED;

	if (mode==COMPLETED)
		return(SUCCESS);

	Log2Phy[Media.Zone][Media.LogBlock]=WriteBlock;
	Media.PhyBlock=ReadBlock;

	if (Media.PhyBlock==NO_ASSIGN)
	{
		Media.PhyBlock=WriteBlock;
		return(SUCCESS);
	}

	if (mode==REQ_ERASE)
	{
		if (Erase_D_PhyOneBlock(us))
		{
			if (ErrCode==ERR_HwError) return(ERROR);
			if (MarkFail_D_PhyOneBlock(us)) return(ERROR);
		}
		else
			Clr_D_Bit(Assign[Media.Zone],Media.PhyBlock);
	}
	else if (MarkFail_D_PhyOneBlock(us))
		return(ERROR);

	Media.PhyBlock=WriteBlock;
	return(SUCCESS);
}

//----- Release_D_WriteBlock() -----------------------------------------
int Release_D_WriteBlock(struct us_data *us)
{
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;
	SectCopyMode=COMPLETED;
	Media.PhyBlock=WriteBlock;

	if (MarkFail_D_PhyOneBlock(us))
		return(ERROR);

	Media.PhyBlock=ReadBlock;
	return(SUCCESS);
}

//SmartMedia Physical Sector Data Copy Subroutine
//----- Copy_D_PhyOneSect() --------------------------------------------
int Copy_D_PhyOneSect(struct us_data *us)
{
	int           i;
	DWORD  err, retry;
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	//printk("Copy_D_PhyOneSect --- Secotr = %x\n", Media.Sector);
	if (ReadBlock!=NO_ASSIGN)
	{
		Media.PhyBlock=ReadBlock;
		for(retry=0; retry<2; retry++)
		{
			if (retry!=0)
			{
				Ssfdc_D_Reset(us);
				if (Ssfdc_D_ReadCisSect(us,WorkBuf,WorkRedund))
				{ ErrCode = ERR_HwError; MediaChange=ERROR; return(ERROR); }

				if (Check_D_CISdata(WorkBuf,WorkRedund))
				{ ErrCode = ERR_HwError; MediaChange=ERROR; return(ERROR); }
			}

			if (Ssfdc_D_ReadSect(us,WorkBuf,WorkRedund))
			{ ErrCode = ERR_HwError; MediaChange=ERROR; return(ERROR); }
			if (Check_D_DataStatus(WorkRedund))
			{ err=ERROR; break; }
			if (!Check_D_ReadError(WorkRedund))
			{ err=SUCCESS; break; }
			if (!Check_D_Correct(WorkBuf,WorkRedund))
			{ err=SUCCESS; break; }

			err=ERROR;
			SectCopyMode=REQ_FAIL;
		}
	}
	else
	{
		err=SUCCESS;
		for(i=0; i<SECTSIZE; i++)
			WorkBuf[i]=DUMMY_DATA;
		Clr_D_RedundantData(WorkRedund);
	}

	Set_D_LogBlockAddr(WorkRedund);
	if (err==ERROR)
	{
		Set_D_RightECC(WorkRedund);
		Set_D_DataStaus(WorkRedund);
	}

	Media.PhyBlock=WriteBlock;

	if (Ssfdc_D_WriteSectForCopy(us, WorkBuf, WorkRedund))
	{ ErrCode = ERR_HwError; MediaChange=ERROR; return(ERROR); }
	if (Ssfdc_D_CheckStatus())
	{ ErrCode = ERR_WriteFault; return(ERROR); }

	Media.PhyBlock=ReadBlock;
	return(SUCCESS);
}

//SmartMedia Physical Sector Read/Write/Erase Subroutine
//----- Read_D_PhyOneSect() --------------------------------------------
int Read_D_PhyOneSect(struct us_data *us, WORD count, BYTE *buf)
{
	int           i;
	DWORD  retry;
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	if (Media.PhyBlock==NO_ASSIGN)
	{
		for(i=0; i<SECTSIZE; i++)
			*buf++=DUMMY_DATA;
		return(SUCCESS);
	}

	for(retry=0; retry<2; retry++)
	{
		if (retry!=0)
		{
			Ssfdc_D_Reset(us);

			if (Ssfdc_D_ReadCisSect(us,WorkBuf,WorkRedund))
			{ ErrCode = ERR_HwError; MediaChange=ERROR; return(ERROR); }
			if (Check_D_CISdata(WorkBuf,WorkRedund))
			{ ErrCode = ERR_HwError; MediaChange=ERROR; return(ERROR); }
		}

		//if (Ssfdc_D_ReadSect(fdoExt,buf,Redundant))
		if (Ssfdc_D_ReadBlock(us,count,buf,Redundant))
		{ ErrCode = ERR_HwError; MediaChange=ERROR; return(ERROR); }
		if (Check_D_DataStatus(Redundant))
		{ ErrCode = ERR_DataStatus; return(ERROR); }

		if (!Check_D_ReadError(Redundant))
			return(SUCCESS);

		if (!Check_D_Correct(buf,Redundant))
		{ ErrCode = ERR_CorReadErr; return(ERROR); }
	}

	ErrCode = ERR_EccReadErr;
	return(ERROR);
}
/*
//----- Write_D_PhyOneSect() -------------------------------------------
int Write_D_PhyOneSect(PFDO_DEVICE_EXTENSION fdoExt, WORD count, BYTE *buf)
{
    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
    ADDRESS_T   bb = (ADDRESS_T) &Media;

    //if (Ssfdc_D_WriteSect(fdoExt,buf,Redundant))
    if (Ssfdc_D_WriteBlock(fdoExt,count,buf,Redundant))
    { ErrCode = ERR_HwError; MediaChange=ERROR; return(ERROR); }
    if (Ssfdc_D_CheckStatus())
    { ErrCode = ERR_WriteFault; return(ERROR); }

    return(SUCCESS);
}
*/
//----- Erase_D_PhyOneBlock() ------------------------------------------
int Erase_D_PhyOneBlock(struct us_data *us)
{
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	if (Ssfdc_D_EraseBlock(us))
	{ ErrCode = ERR_HwError; MediaChange=ERROR; return(ERROR); }
	if (Ssfdc_D_CheckStatus())
	{ ErrCode = ERR_WriteFault; return(ERROR); }

	return(SUCCESS);
}

//SmartMedia Physical Format Check Local Subroutine
//----- Set_D_PhyFmtValue() --------------------------------------------
int Set_D_PhyFmtValue(struct us_data *us)
{
//    PPDO_DEVICE_EXTENSION   pdoExt;
//    BYTE      idcode[4];
//    DWORD     UserDefData_1, UserDefData_2, Data, mask;
//
//    //if (!fdoExt->ChildDeviceObject)       return(ERROR);
//    //pdoExt = fdoExt->ChildDeviceObject->DeviceExtension;
//
//    Ssfdc_D_ReadID(idcode, READ_ID_1);
//
    //if (Set_D_SsfdcModel(idcode[1]))
    if (Set_D_SsfdcModel(us->SM_DeviceID))
        return(ERROR);

//    //Use Multi-function pin to differentiate SM and xD.
//    UserDefData_1 = ReadPCIReg(fdoExt->BusID, fdoExt->DevID, fdoExt->FuncID, PCI_REG_USER_DEF) & 0x80;
//    if (UserDefData_1)
//    {
//       if ( READ_PORT_BYTE(SM_REG_INT_STATUS) & 0x80 )      fdoExt->DiskType = DISKTYPE_XD;
//       if ( READ_PORT_BYTE(SM_REG_INT_STATUS) & 0x40 )      fdoExt->DiskType = DISKTYPE_SM;
//
//       if ( IsXDCompliance && (fdoExt->DiskType == DISKTYPE_XD) )
//       {
//          Ssfdc_D_ReadID(idcode, READ_ID_3);
//          if (idcode[2] != 0xB5)
//             return(ERROR);
//       }
//    }
//
//    //Use GPIO to differentiate SM and xD.
//    UserDefData_2 = ReadPCIReg(fdoExt->BusID, fdoExt->DevID, fdoExt->FuncID, PCI_REG_USER_DEF) >> 8;
//    if ( UserDefData_2 )
//    {
//       Data = ReadPCIReg(fdoExt->BusID, fdoExt->DevID, 0, 0xAC);
//
//       mask = 1 << (UserDefData_2-1);
//       // 1 : xD , 0 : SM
//       if ( Data & mask)
//          fdoExt->DiskType = DISKTYPE_XD;
//       else
//          fdoExt->DiskType = DISKTYPE_SM;
//
//       if ( IsXDCompliance && (fdoExt->DiskType == DISKTYPE_XD) )
//       {
//          Ssfdc_D_ReadID(idcode, READ_ID_3);
//          if (idcode[2] != 0xB5)
//             return(ERROR);
//       }
//    }
//
//    if ( !(UserDefData_1 | UserDefData_2) )
//    {
//      // Use UserDefine Register to differentiate SM and xD.
//      Ssfdc_D_ReadID(idcode, READ_ID_3);
//
//      if (idcode[2] == 0xB5)
//         fdoExt->DiskType = DISKTYPE_XD;
//      else
//      {
//          if (!IsXDCompliance)
//             fdoExt->DiskType = DISKTYPE_SM;
//          else
//             return(ERROR);
//      }
//
//      if (fdoExt->UserDef_DiskType == 0x04)  fdoExt->DiskType = DISKTYPE_XD;
//      if (fdoExt->UserDef_DiskType == 0x08)  fdoExt->DiskType = DISKTYPE_SM;
//    }
//
//    if (!fdoExt->UserDef_DisableWP)
//    {
//       if (fdoExt->DiskType == DISKTYPE_SM)
//       {
//           if (Check_D_SsfdcWP())
//              Ssfdc.Attribute|=WP;
//       }
//    }

    return(SUCCESS);
}

//----- Search_D_CIS() -------------------------------------------------
int Search_D_CIS(struct us_data *us)
{
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	Media.Zone=0; Media.Sector=0;

	for (Media.PhyBlock=0; Media.PhyBlock<(Ssfdc.MaxBlocks-Ssfdc.MaxLogBlocks-1); Media.PhyBlock++)
	{
		if (Ssfdc_D_ReadRedtData(us, Redundant))
		{
			Ssfdc_D_Reset(us);
			return(ERROR);
		}

		if (!Check_D_FailBlock(Redundant))
			break;
	}

	if (Media.PhyBlock==(Ssfdc.MaxBlocks-Ssfdc.MaxLogBlocks-1))
	{
		Ssfdc_D_Reset(us);
		return(ERROR);
	}

	while (Media.Sector<CIS_SEARCH_SECT)
	{
		if (Media.Sector)
		{
			if (Ssfdc_D_ReadRedtData(us, Redundant))
			{
				Ssfdc_D_Reset(us);
				return(ERROR);
			}
		}
		if (!Check_D_DataStatus(Redundant))
		{
			if (Ssfdc_D_ReadSect(us,WorkBuf,Redundant))
			{
				Ssfdc_D_Reset(us);
				return(ERROR);
			}

			if (Check_D_CISdata(WorkBuf,Redundant))
			{
				Ssfdc_D_Reset(us);
				return(ERROR);
			}

			CisArea.PhyBlock=Media.PhyBlock;
			CisArea.Sector=Media.Sector;
			Ssfdc_D_Reset(us);
			return(SUCCESS);
		}

		Media.Sector++;
	}

	Ssfdc_D_Reset(us);
	return(ERROR);
}

//----- Make_D_LogTable() ----------------------------------------------
int Make_D_LogTable(struct us_data *us)
{
	WORD  phyblock,logblock;
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	if (Log2Phy[Media.Zone]==NULL)
	{
		Log2Phy[Media.Zone] = kmalloc(MAX_LOGBLOCK*sizeof(WORD), GFP_KERNEL);
		//printk("ExAllocatePool Zone = %x, Addr = %x\n", Media.Zone, Log2Phy[Media.Zone]);
		if (Log2Phy[Media.Zone]==NULL)
			return(ERROR);
	}

	Media.Sector=0;

	//for(Media.Zone=0; Media.Zone<MAX_ZONENUM; Media.Zone++)
	//for(Media.Zone=0; Media.Zone<Ssfdc.MaxZones; Media.Zone++)
	{
		//printk("Make_D_LogTable --- MediaZone = 0x%x\n", Media.Zone);
		for(Media.LogBlock=0; Media.LogBlock<Ssfdc.MaxLogBlocks; Media.LogBlock++)
			Log2Phy[Media.Zone][Media.LogBlock]=NO_ASSIGN;

		for(Media.PhyBlock=0; Media.PhyBlock<(MAX_BLOCKNUM/8); Media.PhyBlock++)
			Assign[Media.Zone][Media.PhyBlock]=0x00;

		for(Media.PhyBlock=0; Media.PhyBlock<Ssfdc.MaxBlocks; Media.PhyBlock++)
		{
			if ((!Media.Zone) && (Media.PhyBlock<=CisArea.PhyBlock))
			{
				Set_D_Bit(Assign[Media.Zone],Media.PhyBlock);
				continue;
			}

			if (Ssfdc_D_ReadRedtData(us, Redundant))
			{ Ssfdc_D_Reset(us); return(ERROR); }

			if (!Check_D_DataBlank(Redundant))
				continue;

			Set_D_Bit(Assign[Media.Zone],Media.PhyBlock);

			if (Check_D_FailBlock(Redundant))
				continue;

			//if (Check_D_DataStatus(Redundant))
			//    continue;

			if (Load_D_LogBlockAddr(Redundant))
				continue;

			if (Media.LogBlock>=Ssfdc.MaxLogBlocks)
				continue;

			if (Log2Phy[Media.Zone][Media.LogBlock]==NO_ASSIGN)
			{
				Log2Phy[Media.Zone][Media.LogBlock]=Media.PhyBlock;
				continue;
			}

			phyblock     = Media.PhyBlock;
			logblock     = Media.LogBlock;
			Media.Sector = (BYTE)(Ssfdc.MaxSectors-1);

			if (Ssfdc_D_ReadRedtData(us, Redundant))
			{ Ssfdc_D_Reset(us); return(ERROR); }

			if (!Load_D_LogBlockAddr(Redundant))
			{
				if (Media.LogBlock==logblock)
				{
					Media.PhyBlock=Log2Phy[Media.Zone][logblock];

					if (Ssfdc_D_ReadRedtData(us, Redundant))
					{ Ssfdc_D_Reset(us); return(ERROR); }

					Media.PhyBlock=phyblock;

					if (!Load_D_LogBlockAddr(Redundant))
					{
						if (Media.LogBlock!=logblock)
						{
							Media.PhyBlock=Log2Phy[Media.Zone][logblock];
							Log2Phy[Media.Zone][logblock]=phyblock;
						}
					}
					else
					{
						Media.PhyBlock=Log2Phy[Media.Zone][logblock];
						Log2Phy[Media.Zone][logblock]=phyblock;
					}
				}
			}

			Media.Sector=0;

// here Not yet
//#ifdef L2P_ERR_ERASE
//			if (!(Ssfdc.Attribute &MWP))
//			{
//				Ssfdc_D_Reset(fdoExt);
//				if (Ssfdc_D_EraseBlock(fdoExt))
//					return(ERROR);
//
//				if (Ssfdc_D_CheckStatus())
//				{
//					if (MarkFail_D_PhyOneBlock())
//						return(ERROR);
//				}
//				else
//					Clr_D_Bit(Assign[Media.Zone],Media.PhyBlock);
//			}
//#else
//			Ssfdc.Attribute|=MWP;
//#endif
			Media.PhyBlock=phyblock;

		} // End for (Media.PhyBlock<Ssfdc.MaxBlocks)

		AssignStart[Media.Zone]=0;

	} // End for (Media.Zone<MAX_ZONENUM)

	Ssfdc_D_Reset(us);
	return(SUCCESS);
}

//----- MarkFail_D_PhyOneBlock() ---------------------------------------
int MarkFail_D_PhyOneBlock(struct us_data *us)
{
	BYTE sect;
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	sect=Media.Sector;
	Set_D_FailBlock(WorkRedund);
	//Ssfdc_D_WriteRedtMode();

	for(Media.Sector=0; Media.Sector<Ssfdc.MaxSectors; Media.Sector++)
	{
		if (Ssfdc_D_WriteRedtData(us, WorkRedund))
		{
			Ssfdc_D_Reset(us);
			Media.Sector   = sect;
			ErrCode        = ERR_HwError;
			MediaChange = ERROR;
			return(ERROR);
		} // NO Status Check
	}

	Ssfdc_D_Reset(us);
	Media.Sector=sect;
	return(SUCCESS);
}
/*
//
////----- SM_Init() ----------------------------------------------------
//void SM_Init(void)
//{
//    _Hw_D_ClrIntCardChg();
//    _Hw_D_SetIntMask();
//    // For DMA Interrupt
//    _Hw_D_ClrDMAIntCardChg();
//    _Hw_D_SetDMAIntMask();
//}
//
////----- Media_D_EraseAllRedtData() -----------------------------------
//int Media_D_EraseAllRedtData(DWORD Index, BOOLEAN CheckBlock)
//{
//    BYTE    i;
//
//    if (Check_D_MediaPower())
//        return(ErrCode);
//
//    if (Check_D_MediaWP())
//        return(ErrCode);
//
//    for (i=0; i<REDTSIZE; i++)
//        WorkRedund[i] = 0xFF;
//
//    Media.Zone = (BYTE)Index;
//    for (Media.PhyBlock=0; Media.PhyBlock<Ssfdc.MaxBlocks; Media.PhyBlock++)
//    {
//        if ((!Media.Zone) && (Media.PhyBlock<=CisArea.PhyBlock))
//            continue;
//
//        if (Ssfdc_D_EraseBlock(fdoExt))
//        {
//            ErrCode = ERR_HwError;
//            return(ERROR);
//        }
//
//        for(Media.Sector=0; Media.Sector<Ssfdc.MaxSectors; Media.Sector++)
//        {
//            Ssfdc_D_WriteRedtMode();
//
//            if (Ssfdc_D_WriteRedtData(WorkRedund))
//            {
//                Ssfdc_D_Reset(fdoExt);
//                ErrCode        = ERR_HwError;
//                MediaChange    = ERROR;
//                return(ERROR);
//            } // NO Status Check
//        }
//
//        Ssfdc_D_Reset(fdoExt);
//    }
//
//    Ssfdc_D_Reset(fdoExt);
//
//    return(SUCCESS);
//}
//
////----- Media_D_GetMediaInfo() ---------------------------------------
//DWORD Media_D_GetMediaInfo(PFDO_DEVICE_EXTENSION fdoExt, PIOCTL_MEDIA_INFO_IN pParamIn, PIOCTL_MEDIA_INFO_OUT pParamOut)
//{
//    pParamOut->ErrCode = STATUS_CMD_FAIL;
//
//    Init_D_SmartMedia();
//
//    if (Check_D_MediaPower())
//        return (ErrCode==ERR_NoSmartMedia) ? STATUS_CMD_NO_MEDIA : STATUS_CMD_FAIL;
//
//    if (Set_D_PhyFmtValue(fdoExt))
//        return STATUS_CMD_FAIL;
//
//    //usleep(56*1024);
//    if (Search_D_CIS(fdoExt))
//        return STATUS_CMD_FAIL;
//
//    if (Check_D_MediaWP())
//        return STATUS_CMD_MEDIA_WP;
//
//    pParamOut->PageSize  = Ssfdc.MaxSectors;
//    pParamOut->BlockSize = Ssfdc.MaxBlocks;
//    pParamOut->ZoneSize  = Ssfdc.MaxZones;
//
//    return STATUS_CMD_SUCCESS;
//}*/
