#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"
#include <linux/fs.h> 

struct FSM_DeviceFunctionTree FSMFlash_dft;
struct FSM_DeviceTree* FSMFlash_dftv;
struct FSM_SendCmd FSMFlash_fsmsc;
struct FSMFlash_Control FSMFlash_flctl[FSM_FlasherSize];
int FSM_FlashFirmwareOpen(struct FSM_DeviceTree* to_dt,int n)
{
    struct file *f; 
    int numb;
    char filename[100];
    printk( KERN_INFO "Firmware Open" ); 
    sprintf(filename,"/fsm/firmware/t%uv%upv%uk%u.fsmflash",
                        to_dt->dt->type,
                        to_dt->dt->VidDevice,
                        to_dt->dt->PodVidDevice,
                        to_dt->dt->KodDevice);
    f = filp_open( filename, O_RDONLY, 0 ); 
    if( IS_ERR( f ) ) { 
        printk( KERN_INFO "Firmware not found" ); 
        return -1;
    } 
    numb = kernel_read( f, 0, (char*)&FSMFlash_flctl[n].firm, sizeof(struct FSMFirmware) ); 
    filp_close( f, NULL ); 
    if(!(numb)) return -2;
   
    return 0;
}
int FSM_FlashCheckData(int n)
{
    int i=0;
    if(FSMFlash_flctl[n].firm.evec.crc32 != FSMFlash_flctl[n].firm.svec.crc32) return -1;
    if(FSMFlash_flctl[n].firm.evec.size != FSMFlash_flctl[n].firm.svec.size) return -2;
    for(i=0;i<128;i++) 
    {
            if(FSM_crc32NT(0,FSMFlash_flctl[n].firm.dvec[i].Data,1024)!=FSMFlash_flctl[n].firm.dvec[i].crc32)
            {
                return -3-i;
            }
            if(FSMFlash_flctl[n].firm.dvec[i].num!=i)
            {
                return -3-i;
            }
                
    }
    return 0;
}
void FSM_FlashStart(struct FSM_DeviceTree* to_dt)
{     int i=0;
      for(i=0;i<FSM_FlasherSize;i++)
      {
          if((FSMFlash_flctl[i].reg==1)&&(FSMFlash_flctl[i].dt!=0))  if(FSMFlash_flctl[i].dt->IDDevice==to_dt->IDDevice) return;
      }
      for(i=0;i<FSM_FlasherSize;i++)
      {
          if(FSMFlash_flctl[i].reg==0) 
          {
              if(FSM_FlashFirmwareOpen(to_dt,i)!=0) return;
              if(FSM_FlashCheckData(i)!=0) return;
              printk( KERN_INFO "Firmware Run" ); 
              FSMFlash_flctl[i].reg=1;
              FSMFlash_flctl[i].dt=to_dt;
              FSMFlash_flctl[i].state = (char)FSM_Flash_S_Start;
              FSMFlash_fsmsc.cmd=FSMFlash_Start;
              FSMFlash_fsmsc.IDDevice=to_dt->IDDevice;
              memcpy(FSMFlash_fsmsc.Data,(char*)&FSMFlash_flctl[i].firm.svec,sizeof(struct FSMFlahData_StartVector));
              to_dt->TrDev->dt->Proc((char*)&FSMFlash_fsmsc,FSMH_Header_Size_SendCmd+sizeof(struct FSMFlahData_StartVector),to_dt->TrDev,to_dt);
              break;
          }
      }
     
      
}
EXPORT_SYMBOL(FSM_FlashStart);


void FSM_FlashRecive(char* data, short len, struct FSM_DeviceTree* to_dt)
{
    int i;
    
    switch(((struct FSM_SendCmdTS*)data)->cmd)
    {
        case FSMFlash_Start:
        printk( KERN_INFO "Firmware Flash Start" ); 
        FSMFlash_fsmsc.cmd=FSMFlash_Data;
        FSMFlash_fsmsc.IDDevice=to_dt->IDDevice;
        for(i=0;i<FSM_FlasherSize;i++)
        {
          if((FSMFlash_flctl[i].reg==1)&&(FSMFlash_flctl[i].dt!=0))  if(FSMFlash_flctl[i].dt->IDDevice==to_dt->IDDevice)
          {
             memcpy(FSMFlash_fsmsc.Data,(char*)&FSMFlash_flctl[i].firm.dvec[0],sizeof(struct FSMFlahData_DataVector));
             to_dt->TrDev->dt->Proc((char*)&FSMFlash_fsmsc,FSMH_Header_Size_SendCmd+sizeof(struct FSMFlahData_DataVector),to_dt->TrDev,to_dt); 
             FSMFlash_flctl[i].state = (char)FSM_Flash_S_Data;
             return;
          }
        }
        
        break;
        case FSMFlash_Execute:
        printk( KERN_ERR "Firmware Flash Execute" ); 
        FSMFlash_fsmsc.cmd=FSMFlash_Confirm;
        FSMFlash_fsmsc.IDDevice=to_dt->IDDevice;
        for(i=0;i<FSM_FlasherSize;i++)
        {
          if((FSMFlash_flctl[i].reg==1)&&(FSMFlash_flctl[i].dt!=0))  if(FSMFlash_flctl[i].dt->IDDevice==to_dt->IDDevice)
          {
             if(FSMFlash_flctl[i].firm.evec.crc32!=((struct FSMFlahData_DataVerifeVector*)(((struct FSM_SendCmdTS*)data)->Data))->crc32)
             {
             FSMFlash_fsmsc.cmd=FSMFlash_Confirm;
             to_dt->TrDev->dt->Proc((char*)&FSMFlash_fsmsc,FSMH_Header_Size_SendCmd,to_dt->TrDev,to_dt);
             FSMFlash_flctl[i].reg=0;
             FSMFlash_flctl[i].state = (char)FSM_Flash_S_End;
             }
             else
             {
             FSMFlash_fsmsc.cmd=FSMFlash_Execute;
             memcpy(FSMFlash_fsmsc.Data,(char*)&FSMFlash_flctl[i].firm.evec,sizeof(struct FSMFlahData_EndVector));  
             to_dt->TrDev->dt->Proc((char*)&FSMFlash_fsmsc,FSMH_Header_Size_SendCmd+sizeof(struct FSMFlahData_EndVector),to_dt->TrDev,to_dt);
             }
             return;
          }
        }
        break;
        case FSMFlash_Confirm:
        
        break;
        case FSMFlash_Data:
        printk( KERN_INFO "Firmware Flash Data %u,", ((struct FSMFlahData_DataVerifeVector*)(((struct FSM_SendCmdTS*)data)->Data))->num); 
        FSMFlash_fsmsc.cmd=FSMFlash_Data;
        FSMFlash_fsmsc.IDDevice=to_dt->IDDevice;
        for(i=0;i<FSM_FlasherSize;i++)
        {
          if((FSMFlash_flctl[i].reg==1)&&(FSMFlash_flctl[i].dt!=0))  if(FSMFlash_flctl[i].dt->IDDevice==to_dt->IDDevice)
          {
             if(FSMFlash_flctl[i].firm.dvec[((struct FSMFlahData_DataVerifeVector*)(((struct FSM_SendCmdTS*)data)->Data))->num].crc32!=((struct FSMFlahData_DataVerifeVector*)(((struct FSM_SendCmdTS*)data)->Data))->crc32)
             {
             memcpy(FSMFlash_fsmsc.Data,(char*)&FSMFlash_flctl[i].firm.dvec[((struct FSMFlahData_DataVerifeVector*)(((struct FSM_SendCmdTS*)data)->Data))->num],sizeof(struct FSMFlahData_DataVector));
             printk( KERN_INFO "Firmware CRC Eror 0x%08x , 0x%08x",FSMFlash_flctl[i].firm.dvec[((struct FSMFlahData_DataVerifeVector*)(((struct FSM_SendCmdTS*)data)->Data))->num].crc32, ((struct FSMFlahData_DataVerifeVector*)(((struct FSM_SendCmdTS*)data)->Data))->crc32); 
             }
             else
             {
             if(((struct FSMFlahData_DataVerifeVector*)(((struct FSM_SendCmdTS*)data)->Data))->num == 127)
             {
             FSMFlash_fsmsc.cmd=FSMFlash_Execute;
             memcpy(FSMFlash_fsmsc.Data,(char*)&FSMFlash_flctl[i].firm.evec,sizeof(struct FSMFlahData_EndVector));  
             to_dt->TrDev->dt->Proc((char*)&FSMFlash_fsmsc,FSMH_Header_Size_SendCmd+sizeof(struct FSMFlahData_EndVector),to_dt->TrDev,to_dt);
             
             return; 
             }
             else
             {
             memcpy(FSMFlash_fsmsc.Data,(char*)&FSMFlash_flctl[i].firm.dvec[((struct FSMFlahData_DataVerifeVector*)(((struct FSM_SendCmdTS*)data)->Data))->num+1],sizeof(struct FSMFlahData_DataVector));
             }
             }
             to_dt->TrDev->dt->Proc((char*)&FSMFlash_fsmsc,FSMH_Header_Size_SendCmd+sizeof(struct FSMFlahData_DataVector),to_dt->TrDev,to_dt); 
             return;
          }
        }
        break;
    }
}
EXPORT_SYMBOL(FSM_FlashRecive);

void
FSM_FlashDeviceRecive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{

    struct FSM_SendCmdTS* fscts = (struct FSM_SendCmdTS*)data;
    //int i;
    // printk( KERN_INFO "FSM SIOCTL,%u \n",fscts->opcode );

    switch(data[0]) {
    case RegDevice:
        FSM_Statstic_SetStatus(to_dt, "ok");
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        break;
    case PacketFromUserSpace: ///< Отправка команды серверу
        switch(fscts->cmd) 
        {
        case FSM_Flash_CTL_Flash:
        printk( KERN_INFO "Firmware Find" );
        FSMFlash_dftv = FSM_FindDevice(((unsigned short*)fscts->Data)[0]);
        if(FSMFlash_dftv == 0) {
         return;
         printk( KERN_INFO "Firmware ID Dev not Find" );
        }
        FSM_FlashStart(FSMFlash_dftv);
        break;
        case FSM_Flash_CTL_GetStatus:
        fscts->Data[0]=FSMFlash_flctl[fscts->Data[0]].state;
        break;
        }
        break;

     
    }
}

static int __init FSMFlash_init(void)
{
    struct FSM_DeviceRegistr regp;
    FSMFlash_dft.aplayp = 0;
    FSMFlash_dft.type = (unsigned char)StatisticandConfig;
    FSMFlash_dft.VidDevice = (unsigned char)FSMDeviceConfig;
    FSMFlash_dft.PodVidDevice = (unsigned char)FSM_Flash;
    FSMFlash_dft.KodDevice = (unsigned char)CTL_FSM_Flash;
    FSMFlash_dft.Proc = FSM_FlashDeviceRecive;
    FSMFlash_dft.config_len = 0;
    FSM_DeviceClassRegister(FSMFlash_dft);

    regp.IDDevice = FSM_FlashID;
    regp.VidDevice = FSMFlash_dft.VidDevice;
    regp.PodVidDevice = FSMFlash_dft.PodVidDevice;
    regp.KodDevice = FSMFlash_dft.KodDevice;
    regp.type = FSMFlash_dft.type;
    regp.opcode = RegDevice;
    regp.CRC = 0;
    FSM_DeviceRegister(regp);
    
    FSMFlash_fsmsc.opcode=SendCmdGlobalcmdToClient;
    FSMFlash_fsmsc.countparam=1;
    FSMFlash_fsmsc.CRC=0;
    printk(KERN_INFO "FSM Flash loaded\n");
    return 0;
}
static void __exit FSMFlash_exit(void)
{
    FSM_ClassDeRegister(FSMFlash_dft);
    printk(KERN_INFO "FSM Flash module unloaded\n");
}
module_init(FSMFlash_init);
module_exit(FSMFlash_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM Flash  Module");
MODULE_LICENSE("GPL");