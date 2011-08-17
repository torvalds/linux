/*
 * Copyright (c) 2004-2010 Atheros Communications Inc.
 * All rights reserved.
 *
 * This file implements the Atheros PS and patch parser.
 * It implements APIs to parse data buffer with patch and PS information and convert it to HCI commands.
 *
 *
 *
 * ar3kpsparser.c
 *
 *
 *
 * The software source and binaries included in this development package are
 * licensed, not sold. You, or your company, received the package under one
 * or more license agreements. The rights granted to you are specifically
 * listed in these license agreement(s). All other rights remain with Atheros
 * Communications, Inc., its subsidiaries, or the respective owner including
 * those listed on the included copyright notices..  Distribution of any
 * portion of this package must be in strict compliance with the license
 * agreement(s) terms.
 *
 *
 *
 */


#include "ar3kpsparser.h"

#include <linux/ctype.h>
#include <linux/kernel.h>

#define BD_ADDR_SIZE            6
#define WRITE_PATCH             8
#define ENABLE_PATCH            11
#define PS_RESET                2
#define PS_WRITE                1
#define PS_VERIFY_CRC           9
#define CHANGE_BDADDR           15

#define HCI_COMMAND_HEADER      7

#define HCI_EVENT_SIZE          7

#define WRITE_PATCH_COMMAND_STATUS_OFFSET 5

#define PS_RAM_SIZE	2048

#define RAM_PS_REGION           (1<<0)
#define RAM_PATCH_REGION        (1<<1)
#define RAMPS_MAX_PS_DATA_PER_TAG         20000
#define MAX_RADIO_CFG_TABLE_SIZE  244
#define RAMPS_MAX_PS_TAGS_PER_FILE        50

#define PS_MAX_LEN                        500 
#define LINE_SIZE_MAX                     (PS_MAX_LEN *2) 

/* Constant values used by parser */
#define BYTES_OF_PS_DATA_PER_LINE         16
#define RAMPS_MAX_PS_DATA_PER_TAG         20000


/* Number pf PS/Patch entries in an HCI packet */
#define MAX_BYTE_LENGTH                   244

#define SKIP_BLANKS(str) while (*str == ' ') str++

enum MinBootFileFormatE
{
   MB_FILEFORMAT_RADIOTBL,
   MB_FILEFORMAT_PATCH,
   MB_FILEFORMAT_COEXCONFIG
};

enum RamPsSection
{
   RAM_PS_SECTION,
   RAM_PATCH_SECTION,
   RAM_DYN_MEM_SECTION
};

enum eType {
   eHex,
   edecimal
};


typedef struct tPsTagEntry
{
   u32 TagId;
   u32 TagLen;
   u8 *TagData;
} tPsTagEntry, *tpPsTagEntry;

typedef struct tRamPatch
{
   u16 Len;
   u8 *Data;
} tRamPatch, *ptRamPatch;



struct st_ps_data_format {
   enum eType   eDataType;
   bool    bIsArray;
};

struct st_read_status {
    unsigned uTagID;
    unsigned uSection;
    unsigned uLineCount;
    unsigned uCharCount;
    unsigned uByteCount;
};


/* Stores the number of PS Tags */
static u32 Tag_Count = 0;

/* Stores the number of patch commands */
static u32 Patch_Count = 0;
static u32 Total_tag_lenght = 0;
bool BDADDR = false;
u32 StartTagId;

tPsTagEntry PsTagEntry[RAMPS_MAX_PS_TAGS_PER_FILE];
tRamPatch   RamPatch[MAX_NUM_PATCH_ENTRY];


int AthParseFilesUnified(u8 *srcbuffer,u32 srclen, int FileFormat);
char AthReadChar(u8 *buffer, u32 len,u32 *pos);
char *AthGetLine(char *buffer, int maxlen, u8 *srcbuffer,u32 len,u32 *pos);
static int AthPSCreateHCICommand(u8 Opcode, u32 Param1,struct ps_cmd_packet *PSPatchPacket,u32 *index);

/* Function to reads the next character from the input buffer */
char AthReadChar(u8 *buffer, u32 len,u32 *pos)
{
    char Ch;
    if(buffer == NULL || *pos >=len )
    {
        return '\0';
    } else {
        Ch = buffer[*pos];
        (*pos)++;
        return Ch;
    }
}
/* PS parser helper function */
unsigned int uGetInputDataFormat(char *pCharLine, struct st_ps_data_format *pstFormat)
{
    if(pCharLine[0] != '[') {
        pstFormat->eDataType = eHex;
        pstFormat->bIsArray = true;
        return 0;
    }
    switch(pCharLine[1]) {
        case 'H':
        case 'h':
        if(pCharLine[2]==':') {
            if((pCharLine[3]== 'a') || (pCharLine[3]== 'A')) {
                if(pCharLine[4] == ']') {
                    pstFormat->eDataType = eHex;
                    pstFormat->bIsArray = true;
                    pCharLine += 5;
                    return 0;
                }
                else {
                       AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Illegal Data format\n")); //[H:A
                    return 1;
                }
            }
            if((pCharLine[3]== 'S') || (pCharLine[3]== 's')) {
                if(pCharLine[4] == ']') {
                    pstFormat->eDataType = eHex;
                    pstFormat->bIsArray = false;
                    pCharLine += 5;
                    return 0;
                }
                else {
                       AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Illegal Data format\n")); //[H:A
                    return 1;
                }
            }
            else if(pCharLine[3] == ']') {         //[H:]
                pstFormat->eDataType = eHex;
                pstFormat->bIsArray = true;
                pCharLine += 4;
                return 0;
            }
            else {                            //[H:
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Illegal Data format\n"));
                return 1;                    
            }
        }
        else if(pCharLine[2]==']') {    //[H]
            pstFormat->eDataType = eHex;
            pstFormat->bIsArray = true;
            pCharLine += 3;
            return 0;
        }
        else {                      //[H
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Illegal Data format\n"));
            return 1;            
        }
        break;

        case 'A':
        case 'a':
        if(pCharLine[2]==':') {
            if((pCharLine[3]== 'h') || (pCharLine[3]== 'H')) {
                if(pCharLine[4] == ']') {
                    pstFormat->eDataType = eHex;
                    pstFormat->bIsArray = true;
                    pCharLine += 5;
                    return 0;
                }
                else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Illegal Data format 1\n")); //[A:H
                    return 1;
                }
             }
            else if(pCharLine[3]== ']') {         //[A:]
                pstFormat->eDataType = eHex;
                pstFormat->bIsArray = true;
                pCharLine += 4;
                return 0;
            }
            else {                            //[A:
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Illegal Data format 2\n"));
                return 1;                    
            }
        }
        else if(pCharLine[2]==']') {    //[H]
            pstFormat->eDataType = eHex;
            pstFormat->bIsArray = true;
            pCharLine += 3;
            return 0;
        }
        else {                      //[H
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Illegal Data format 3\n"));
            return 1;            
        }
        break;

        case 'S':
        case 's':
        if(pCharLine[2]==':') {
            if((pCharLine[3]== 'h') || (pCharLine[3]== 'H')) {
                if(pCharLine[4] == ']') {
                    pstFormat->eDataType = eHex;
                    pstFormat->bIsArray = true;
                    pCharLine += 5;
                    return 0;
                }
                else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Illegal Data format 5\n")); //[A:H
                    return 1;
                }
             }
            else if(pCharLine[3]== ']') {         //[A:]
                pstFormat->eDataType = eHex;
                pstFormat->bIsArray = true;
                pCharLine += 4;
                return 0;
            }
            else {                            //[A:
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Illegal Data format 6\n"));
                return 1;                    
            }
        }
        else if(pCharLine[2]==']') {    //[H]
            pstFormat->eDataType = eHex;
            pstFormat->bIsArray = true;
            pCharLine += 3;
            return 0;
        }
        else {                      //[H
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Illegal Data format 7\n"));
            return 1;            
        }
        break;
    
        default:
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Illegal Data format 8\n"));
        return 1;
    }
}

unsigned int uReadDataInSection(char *pCharLine, struct st_ps_data_format stPS_DataFormat)
{
    char *pTokenPtr = pCharLine;

    if(pTokenPtr[0] == '[') {
        while(pTokenPtr[0] != ']' && pTokenPtr[0] != '\0') {
            pTokenPtr++;
        }
        if(pTokenPtr[0] == '\0') {
            return (0x0FFF);
        }
        pTokenPtr++;
            

    }
    if(stPS_DataFormat.eDataType == eHex) {
        if(stPS_DataFormat.bIsArray == true) {
            //Not implemented
            return (0x0FFF);
        }
        else {
            return (A_STRTOL(pTokenPtr, NULL, 16));
        }
    }
    else {
        //Not implemented
        return (0x0FFF);
    }
}
int AthParseFilesUnified(u8 *srcbuffer,u32 srclen, int FileFormat)
{
   char *Buffer;
   char *pCharLine;
   u8 TagCount;
   u16 ByteCount;
   u8 ParseSection=RAM_PS_SECTION;
   u32 pos;



   int uReadCount;
   struct st_ps_data_format stPS_DataFormat;
   struct st_read_status   stReadStatus = {0, 0, 0,0};
   pos = 0;
   Buffer = NULL;

   if (srcbuffer == NULL || srclen == 0)
   {
      AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Could not open .\n"));
      return A_ERROR;
   }
   TagCount = 0;
   ByteCount = 0;
   Buffer = A_MALLOC(LINE_SIZE_MAX + 1);
   if(NULL == Buffer) {
       return A_ERROR;
   }
   if (FileFormat == MB_FILEFORMAT_PATCH)
   {
      int LineRead = 0;
      while((pCharLine = AthGetLine(Buffer, LINE_SIZE_MAX, srcbuffer,srclen,&pos)) != NULL)
      {

         SKIP_BLANKS(pCharLine);

         // Comment line or empty line
         if ((pCharLine[0] == '/') && (pCharLine[1] == '/'))
         {
            continue;
         }
         
         if ((pCharLine[0] == '#')) { 
             if (stReadStatus.uSection != 0)
             {
                 AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("error\n"));
                 if(Buffer != NULL) {
                     kfree(Buffer);
                 }
                 return A_ERROR;
             }
             else {
                 stReadStatus.uSection = 1;
                 continue;
             }
         }
         if ((pCharLine[0] == '/') && (pCharLine[1] == '*'))
         {
            pCharLine+=2;
            SKIP_BLANKS(pCharLine);

            if(!strncmp(pCharLine,"PA",2)||!strncmp(pCharLine,"Pa",2)||!strncmp(pCharLine,"pa",2))
                ParseSection=RAM_PATCH_SECTION;

            if(!strncmp(pCharLine,"DY",2)||!strncmp(pCharLine,"Dy",2)||!strncmp(pCharLine,"dy",2))
                ParseSection=RAM_DYN_MEM_SECTION;

            if(!strncmp(pCharLine,"PS",2)||!strncmp(pCharLine,"Ps",2)||!strncmp(pCharLine,"ps",2))
                ParseSection=RAM_PS_SECTION;

            LineRead = 0;
            stReadStatus.uSection = 0;

            continue;
    }
         
         switch(ParseSection)
         {
             case RAM_PS_SECTION:
             {
                 if (stReadStatus.uSection == 1)  //TagID
                 {
                    SKIP_BLANKS(pCharLine);
                    if(uGetInputDataFormat(pCharLine, &stPS_DataFormat)) {
                        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("uGetInputDataFormat fail\n"));
                     if(Buffer != NULL) {
                             kfree(Buffer);
                     }
                        return A_ERROR;
                    }    
                    //pCharLine +=5;
                    PsTagEntry[TagCount].TagId = uReadDataInSection(pCharLine, stPS_DataFormat);                            
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" TAG ID %d \n",PsTagEntry[TagCount].TagId));

                    //AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("tag # %x\n", PsTagEntry[TagCount].TagId);
                    if (TagCount == 0)
                    {
                       StartTagId = PsTagEntry[TagCount].TagId;
                    }
                    stReadStatus.uSection = 2;
                 }
                 else if (stReadStatus.uSection == 2) //TagLength
                 {
            
                    if(uGetInputDataFormat(pCharLine, &stPS_DataFormat)) {
                        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("uGetInputDataFormat fail \n"));
                     if(Buffer != NULL) {
                             kfree(Buffer);
                     }
                        return A_ERROR;
                    }
                    //pCharLine +=5;
                    ByteCount = uReadDataInSection(pCharLine, stPS_DataFormat);

                    //AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("tag length %x\n", ByteCount));
                    if (ByteCount > LINE_SIZE_MAX/2)
                    {
                     if(Buffer != NULL) {
                             kfree(Buffer);
                     }
                       return A_ERROR;
                    }
                    PsTagEntry[TagCount].TagLen = ByteCount;
                    PsTagEntry[TagCount].TagData = (u8 *)A_MALLOC(ByteCount);
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" TAG Length %d  Tag Index %d \n",PsTagEntry[TagCount].TagLen,TagCount));
                    stReadStatus.uSection = 3;
                    stReadStatus.uLineCount = 0;
                 }
                 else if( stReadStatus.uSection == 3) {  //Data

                    if(stReadStatus.uLineCount == 0) {
                        if(uGetInputDataFormat(pCharLine,&stPS_DataFormat)) {
                            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("uGetInputDataFormat Fail\n"));
                            if(Buffer != NULL) {
                                 kfree(Buffer);
                         }
                            return A_ERROR;
                        }
                        //pCharLine +=5;
                    }
           SKIP_BLANKS(pCharLine);
                    stReadStatus.uCharCount = 0;
            if(pCharLine[stReadStatus.uCharCount] == '[') {
            while(pCharLine[stReadStatus.uCharCount] != ']' && pCharLine[stReadStatus.uCharCount] != '\0' ) {
                            stReadStatus.uCharCount++;
            }
            if(pCharLine[stReadStatus.uCharCount] == ']' ) {
                            stReadStatus.uCharCount++;
            } else {
                            stReadStatus.uCharCount = 0;
            }
            }
                    uReadCount = (ByteCount > BYTES_OF_PS_DATA_PER_LINE)? BYTES_OF_PS_DATA_PER_LINE: ByteCount;
                    //AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" "));
                    if((stPS_DataFormat.eDataType == eHex) && stPS_DataFormat.bIsArray == true) {
                       while(uReadCount > 0) {
                           PsTagEntry[TagCount].TagData[stReadStatus.uByteCount] =
                                                     (u8)(hex_to_bin(pCharLine[stReadStatus.uCharCount]) << 4)
                                                     | (u8)(hex_to_bin(pCharLine[stReadStatus.uCharCount + 1]));

                           PsTagEntry[TagCount].TagData[stReadStatus.uByteCount+1] =
                                                     (u8)(hex_to_bin(pCharLine[stReadStatus.uCharCount + 3]) << 4)
                                                     | (u8)(hex_to_bin(pCharLine[stReadStatus.uCharCount + 4]));

                           stReadStatus.uCharCount += 6; // read two bytes, plus a space;
                           stReadStatus.uByteCount += 2;
                           uReadCount -= 2;
                       }
                       if(ByteCount > BYTES_OF_PS_DATA_PER_LINE) {
                              ByteCount -= BYTES_OF_PS_DATA_PER_LINE;
                       }
                       else {
                          ByteCount = 0;
                       }
                    }
                    else {
                        //to be implemented
                    }

                    stReadStatus.uLineCount++;
                    
                    if(ByteCount == 0) {
                        stReadStatus.uSection = 0;
                        stReadStatus.uCharCount = 0;
                        stReadStatus.uLineCount = 0;
                        stReadStatus.uByteCount = 0;
                    }
                    else { 
                        stReadStatus.uCharCount = 0;
                    }

                    if((stReadStatus.uSection == 0)&&(++TagCount == RAMPS_MAX_PS_TAGS_PER_FILE))
                    {
                       AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("\n Buffer over flow PS File too big!!!"));
                       if(Buffer != NULL) {
                           kfree(Buffer);
                       }
                       return A_ERROR;
                       //Sleep (3000);
                       //exit(1);
                    }
        
                 }
             }

             break;
             default:
             {
                   if(Buffer != NULL) {
                       kfree(Buffer);
                   }
                   return A_ERROR;
             }
             break;
         }
         LineRead++;
      }
      Tag_Count = TagCount;
      AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Number of Tags %d\n", Tag_Count));
   }


   if (TagCount > RAMPS_MAX_PS_TAGS_PER_FILE)
   {

      if(Buffer != NULL) {
           kfree(Buffer);
      }
      return A_ERROR;
   }

   if(Buffer != NULL) {
        kfree(Buffer);
   }
   return 0;

}



/********************/


int GetNextTwoChar(u8 *srcbuffer,u32 len, u32 *pos, char *buffer)
{
    unsigned char ch;

    ch = AthReadChar(srcbuffer,len,pos);
    if(ch != '\0' && isxdigit(ch)) {
        buffer[0] =  ch;
    } else 
    {
        return A_ERROR;
    }
    ch = AthReadChar(srcbuffer,len,pos);
    if(ch != '\0' && isxdigit(ch)) {
        buffer[1] =  ch;
    } else 
    {
        return A_ERROR;
    }
    return 0;
}

int AthDoParsePatch(u8 *patchbuffer, u32 patchlen)
{

    char Byte[3];
    char Line[MAX_BYTE_LENGTH + 1];
    int    ByteCount,ByteCount_Org;
    int count;
    int i,j,k;
    int data;
    u32 filepos;
    Byte[2] = '\0';
    j = 0;
    filepos = 0;
    Patch_Count = 0;

    while(NULL != AthGetLine(Line,MAX_BYTE_LENGTH,patchbuffer,patchlen,&filepos)) {
        if(strlen(Line) <= 1 || !isxdigit(Line[0])) {
            continue;
        } else {
            break;
        }
    }
    ByteCount = A_STRTOL(Line, NULL, 16);
    ByteCount_Org = ByteCount;

    while(ByteCount > MAX_BYTE_LENGTH){

        /* Handle case when the number of patch buffer is more than the 20K */
        if(MAX_NUM_PATCH_ENTRY == Patch_Count) {
            for(i = 0; i < Patch_Count; i++) {
                kfree(RamPatch[i].Data);
            }
            return A_ERROR;
        }
        RamPatch[Patch_Count].Len= MAX_BYTE_LENGTH;
        RamPatch[Patch_Count].Data = (u8 *)A_MALLOC(MAX_BYTE_LENGTH);
        Patch_Count ++;


        ByteCount= ByteCount - MAX_BYTE_LENGTH;
    }

    RamPatch[Patch_Count].Len= (ByteCount & 0xFF);
    if(ByteCount != 0) {
        RamPatch[Patch_Count].Data = (u8 *)A_MALLOC(ByteCount);
        Patch_Count ++;
    }
    count = 0;
    while(ByteCount_Org > MAX_BYTE_LENGTH){
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" Index [%d]\n",j));
        for (i = 0,k=0; i < MAX_BYTE_LENGTH*2; i += 2,k++,count +=2) {
            if(GetNextTwoChar(patchbuffer,patchlen,&filepos,Byte) == A_ERROR) {
                return A_ERROR;
            }
            data = A_STRTOUL(&Byte[0], NULL, 16);
            RamPatch[j].Data[k] = (data & 0xFF);


        }
        j++;
        ByteCount_Org = ByteCount_Org - MAX_BYTE_LENGTH;
    }
    if(j == 0){
        j++;
    }
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" Index [%d]\n",j));
    for (k=0; k < ByteCount_Org; i += 2,k++,count+=2) {
        if(GetNextTwoChar(patchbuffer,patchlen,&filepos,Byte) == A_ERROR) {
            return A_ERROR;
        }
        data = A_STRTOUL(Byte, NULL, 16);
        RamPatch[j].Data[k] = (data & 0xFF);


    }
    return 0;
}


/********************/
int AthDoParsePS(u8 *srcbuffer, u32 srclen)
{
    int status;
    int i;
    bool BDADDR_Present = false;

    Tag_Count = 0;

    Total_tag_lenght = 0;
    BDADDR = false;


    status = A_ERROR;

    if(NULL != srcbuffer && srclen != 0)
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("File Open Operation Successful\n"));

        status = AthParseFilesUnified(srcbuffer,srclen,MB_FILEFORMAT_PATCH); 
    }
    


        if(Tag_Count == 0){
                Total_tag_lenght = 10;

        }
        else{
                for(i=0; i<Tag_Count; i++){
                        if(PsTagEntry[i].TagId == 1){
                                BDADDR_Present = true;
                                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BD ADDR is present in Patch File \r\n"));

                        }
                        if(PsTagEntry[i].TagLen % 2 == 1){
                                Total_tag_lenght = Total_tag_lenght + PsTagEntry[i].TagLen + 1;
                        }
                        else{
                                Total_tag_lenght = Total_tag_lenght + PsTagEntry[i].TagLen;
                        }

                }
        }

        if(Tag_Count > 0 && !BDADDR_Present){
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BD ADDR is not present adding 10 extra bytes \r\n"));
                Total_tag_lenght=Total_tag_lenght + 10;
        }
        Total_tag_lenght = Total_tag_lenght+ 10 + (Tag_Count*4);
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("** Total Length %d\n",Total_tag_lenght));


    return status;
}
char *AthGetLine(char *buffer, int maxlen, u8 *srcbuffer,u32 len,u32 *pos)
{

    int count;
    static short flag;
    char CharRead;
    count = 0;
    flag = A_ERROR;

    do
    {
        CharRead = AthReadChar(srcbuffer,len,pos);
        if( CharRead == '\0' ) {
            buffer[count+1] = '\0';
            if(count == 0) {
                return NULL;
            }
            else {
                return buffer;
            }
        }

        if(CharRead == 13) {
        } else if(CharRead == 10) {
            buffer[count] ='\0';  
            flag = A_ERROR;
            return buffer;
        }else {
            buffer[count++] = CharRead;
        }

    }
    while(count < maxlen-1 && CharRead != '\0');
    buffer[count] = '\0';

    return buffer;
}

static void LoadHeader(u8 *HCI_PS_Command,u8 opcode,int length,int index){

        HCI_PS_Command[0]= 0x0B;
        HCI_PS_Command[1]= 0xFC;
        HCI_PS_Command[2]= length + 4;
        HCI_PS_Command[3]= opcode;
        HCI_PS_Command[4]= (index & 0xFF);
        HCI_PS_Command[5]= ((index>>8) & 0xFF);
        HCI_PS_Command[6]= length;
}

/////////////////////////
//
int AthCreateCommandList(struct ps_cmd_packet **HciPacketList, u32 *numPackets)
{

    u8 count;
    u32 NumcmdEntry = 0;

    u32 Crc = 0;
    *numPackets = 0;


    if(Patch_Count > 0)
            Crc |= RAM_PATCH_REGION;
    if(Tag_Count > 0)
            Crc |= RAM_PS_REGION;
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("PS Thread Started CRC %x Patch Count %d  Tag Count %d \n",Crc,Patch_Count,Tag_Count));
    
    if(Patch_Count || Tag_Count ){
    NumcmdEntry+=(2 + Patch_Count + Tag_Count); /* CRC Packet + PS Reset Packet  + Patch List + PS List*/
        if(Patch_Count > 0) {
            NumcmdEntry++; /* Patch Enable Command */
        }
           AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Num Cmd Entries %d Size  %d  \r\n",NumcmdEntry,(u32)sizeof(struct ps_cmd_packet) * NumcmdEntry));
        (*HciPacketList) = A_MALLOC(sizeof(struct ps_cmd_packet) * NumcmdEntry);
    if(NULL == *HciPacketList) {
               AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("memory allocation failed  \r\n"));
        }
        AthPSCreateHCICommand(PS_VERIFY_CRC,Crc,*HciPacketList,numPackets);
        if(Patch_Count > 0){
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("*** Write Patch**** \r\n"));
                AthPSCreateHCICommand(WRITE_PATCH,Patch_Count,*HciPacketList,numPackets);
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("*** Enable Patch**** \r\n"));
                AthPSCreateHCICommand(ENABLE_PATCH,0,*HciPacketList,numPackets);
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("*** PS Reset**** %d[0x%x] \r\n",PS_RAM_SIZE,PS_RAM_SIZE));
		AthPSCreateHCICommand(PS_RESET,PS_RAM_SIZE,*HciPacketList,numPackets);
        if(Tag_Count > 0){
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("*** PS Write**** \r\n"));
                AthPSCreateHCICommand(PS_WRITE,Tag_Count,*HciPacketList,numPackets);
        }    
    }
    if(!BDADDR){
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BD ADDR not present \r\n"));
    
    }
    for(count = 0; count < Patch_Count; count++) {

        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Freeing Patch Buffer %d \r\n",count));
	kfree(RamPatch[count].Data);
    }

    for(count = 0; count < Tag_Count; count++) {

        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Freeing PS Buffer %d \r\n",count));
        kfree(PsTagEntry[count].TagData);
    }

/* 
 *  SDIO Transport uses synchronous mode of data transfer 
 *  So, AthPSOperations() call returns only after receiving the 
 *  command complete event.
 */
    return *numPackets;
}


////////////////////////

/////////////
static int AthPSCreateHCICommand(u8 Opcode, u32 Param1,struct ps_cmd_packet *PSPatchPacket,u32 *index)
{
    u8 *HCI_PS_Command;
    u32 Length;
    int i,j;
    
    switch(Opcode)
    {
    case WRITE_PATCH:


         for(i=0;i< Param1;i++){

             HCI_PS_Command = (u8 *) A_MALLOC(RamPatch[i].Len+HCI_COMMAND_HEADER);
             AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Allocated Buffer Size %d\n",RamPatch[i].Len+HCI_COMMAND_HEADER));
                 if(HCI_PS_Command == NULL){
                     AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("MALLOC Failed\r\n"));
                         return A_ERROR;
                 }
                 memset (HCI_PS_Command, 0, RamPatch[i].Len+HCI_COMMAND_HEADER);
                 LoadHeader(HCI_PS_Command,Opcode,RamPatch[i].Len,i);
                 for(j=0;j<RamPatch[i].Len;j++){
                        HCI_PS_Command[HCI_COMMAND_HEADER+j]=RamPatch[i].Data[j];
                 }
                 PSPatchPacket[*index].Hcipacket = HCI_PS_Command;
                 PSPatchPacket[*index].packetLen = RamPatch[i].Len+HCI_COMMAND_HEADER;
                 (*index)++;

          
         }

    break;

    case ENABLE_PATCH:


         Length = 0;
         i= 0;
         HCI_PS_Command = (u8 *) A_MALLOC(Length+HCI_COMMAND_HEADER);
         if(HCI_PS_Command == NULL){
             AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("MALLOC Failed\r\n"));
            return A_ERROR;
         }

         memset (HCI_PS_Command, 0, Length+HCI_COMMAND_HEADER);
         LoadHeader(HCI_PS_Command,Opcode,Length,i);
         PSPatchPacket[*index].Hcipacket = HCI_PS_Command;
         PSPatchPacket[*index].packetLen = Length+HCI_COMMAND_HEADER;
         (*index)++;

    break;

    case PS_RESET:
                        Length = 0x06;
                        i=0;
                        HCI_PS_Command = (u8 *) A_MALLOC(Length+HCI_COMMAND_HEADER);
                        if(HCI_PS_Command == NULL){
                                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("MALLOC Failed\r\n"));
                                return A_ERROR;
                        }
                        memset (HCI_PS_Command, 0, Length+HCI_COMMAND_HEADER);
                        LoadHeader(HCI_PS_Command,Opcode,Length,i);
                        HCI_PS_Command[7]= 0x00;
                        HCI_PS_Command[Length+HCI_COMMAND_HEADER -2]= (Param1 & 0xFF);
                        HCI_PS_Command[Length+HCI_COMMAND_HEADER -1]= ((Param1 >>  8) & 0xFF);
                 PSPatchPacket[*index].Hcipacket = HCI_PS_Command;
                 PSPatchPacket[*index].packetLen = Length+HCI_COMMAND_HEADER;
                 (*index)++;

    break;

    case PS_WRITE:
                       for(i=0;i< Param1;i++){
                                if(PsTagEntry[i].TagId ==1)
                                        BDADDR = true;

                                HCI_PS_Command = (u8 *) A_MALLOC(PsTagEntry[i].TagLen+HCI_COMMAND_HEADER);
                                if(HCI_PS_Command == NULL){
                                        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("MALLOC Failed\r\n"));
                                        return A_ERROR;
                                }

                                memset (HCI_PS_Command, 0, PsTagEntry[i].TagLen+HCI_COMMAND_HEADER);
                                LoadHeader(HCI_PS_Command,Opcode,PsTagEntry[i].TagLen,PsTagEntry[i].TagId);

                                for(j=0;j<PsTagEntry[i].TagLen;j++){
                                        HCI_PS_Command[HCI_COMMAND_HEADER+j]=PsTagEntry[i].TagData[j];
                                }

                     PSPatchPacket[*index].Hcipacket = HCI_PS_Command;
                     PSPatchPacket[*index].packetLen = PsTagEntry[i].TagLen+HCI_COMMAND_HEADER;
                     (*index)++;

                        }

    break;


    case PS_VERIFY_CRC:
                        Length = 0x0;

                        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("VALUE of CRC:%d At index %d\r\n",Param1,*index));

                        HCI_PS_Command = (u8 *) A_MALLOC(Length+HCI_COMMAND_HEADER);
                        if(HCI_PS_Command == NULL){
                                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("MALLOC Failed\r\n"));
                                return A_ERROR;
                        }
                        memset (HCI_PS_Command, 0, Length+HCI_COMMAND_HEADER);
                        LoadHeader(HCI_PS_Command,Opcode,Length,Param1);

                 PSPatchPacket[*index].Hcipacket = HCI_PS_Command;
                 PSPatchPacket[*index].packetLen = Length+HCI_COMMAND_HEADER;
                 (*index)++;

    break;

    case CHANGE_BDADDR:
    break;
    }
    return 0;
}
int AthFreeCommandList(struct ps_cmd_packet **HciPacketList, u32 numPackets)
{
    int i;
    if(*HciPacketList == NULL) {
        return A_ERROR;
    }
    for(i = 0; i < numPackets;i++) {
        kfree((*HciPacketList)[i].Hcipacket);
    }  
    kfree(*HciPacketList);
    return 0;
}
