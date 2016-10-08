#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"

struct FSM_DeviceFunctionTree FSMDevSet_dft;
struct SettingTreeInfo FSMDevSetTree[FSM_DeviceSettingTreeSize];
struct FSM_GetTreeList FSMDevSet_fsmgetts;
struct FSM_SetSetting FSMDevSet_fsmtset;
struct FSM_GetSetting FSMDevSet_fsmtget;
struct FSM_DeviceTree* FSMDevSet_dftv;
void FSM_SendReuestDevTree(struct FSM_DeviceTree* to_dt)
{
    FSMDevSet_fsmgetts.CRC = 0;
    FSMDevSet_fsmgetts.IDDevice = to_dt->IDDevice;
    FSMDevSet_fsmgetts.opcode = FSM_Setting_GetTree;
    to_dt->TrDev->dt->Proc((char*)&FSMDevSet_fsmgetts, FSMH_Header_Size_GetTreeList, to_dt->TrDev, to_dt);
}
EXPORT_SYMBOL(FSM_SendReuestDevTree);

void FSM_SetTreeAdd(struct FSM_DeviceTree* to_dt)
{
    int i = 0;
    for(i = 0; i < FSM_DeviceSettingTreeSize; i++) {
        if((FSMDevSetTree[i].id == to_dt->IDDevice) && (FSMDevSetTree[i].reg == 1)) {
            FSM_SendReuestDevTree(to_dt);
            return;
        }
    }
    for(i = 0; i < FSM_DeviceSettingTreeSize; i++) {
        if(FSMDevSetTree[i].reg == 0) {
            FSMDevSetTree[i].id = to_dt->IDDevice;
            FSMDevSetTree[i].reg = 1;
            FSMDevSetTree[i].dt = to_dt;
            FSM_SendReuestDevTree(to_dt);
            return;
        }
    }
}
EXPORT_SYMBOL(FSM_SetTreeAdd);

void FSM_TreeRecive(char* data, short len, struct FSM_DeviceTree* to_dt)
{
    int i;
    struct FSM_SetTreeElementFS* fsmtel=((struct FSM_SetTreeElementFS*)(((struct FSM_AnsGetTreeList*)data)->Data));
    struct FSM_AnsGetSetting* fsmtans =(struct FSM_AnsGetSetting*)data;
    
    switch(data[0]) {
    case Ans_FSM_Setting_GetTree:
        for(i = 0; i < FSM_DeviceSettingTreeSize; i++) {
            if((FSMDevSetTree[i].id == to_dt->IDDevice) && (FSMDevSetTree[i].reg == 1)) 
            {
                FSMDevSetTree[i].type = (char)(((struct FSM_AnsGetTreeList*)data)->size);
                
                memcpy(&FSMDevSetTree[i].fsmdtl[fsmtel->iid],((struct FSM_AnsGetTreeList*)data)->Data,sizeof(struct FSM_SetTreeElementFS));
                return;
            }
        }
        break;
    case Ans_FSM_Setting_Read:
        for(i = 0; i < FSM_DeviceSettingTreeSize; i++) {
            if((FSMDevSetTree[i].id == to_dt->IDDevice) && (FSMDevSetTree[i].reg == 1)) 
            {
                memcpy(FSMDevSetTree[i].fsm_tr_temp,fsmtans->Data,fsmtans->size);
                FSMDevSetTree[i].fsm_tr_size =fsmtans->size;
                return;
            }
        }
        break;
    }
}
EXPORT_SYMBOL(FSM_TreeRecive);

void
FSM_SettingTreeControlDeviceRecive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{

    struct FSM_SendCmdTS* fscts = (struct FSM_SendCmdTS*)data;
    int i;
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
        case FSM_DevTreeSetGet:
        i = ((struct FSM_SetTreeGetList*)fscts->Data)->IDDevice;
        memcpy(fscts->Data,&FSMDevSetTree[i].fsmdtl[(((struct FSM_SetTreeGetList*)fscts->Data)->iid)],sizeof(struct FSM_SetTreeElementFS));
        break;
        case FSM_DevTreeSetGetCount:
         for(i = 0; i < FSM_DeviceSettingTreeSize; i++) {
                if((FSMDevSetTree[i].id == ((unsigned short*)fscts->Data)[0]) && (FSMDevSetTree[i].reg == 1)) {
                    ((struct FSM_SetTreeGetListCount*)fscts->Data)->IDDevice=i;
                    ((struct FSM_SetTreeGetListCount*)fscts->Data)->count=FSMDevSetTree[i].type;
                    return;
                }
            }
        break;
        case FSM_DevTreeSetWrite:
        FSMDevSet_fsmtset.IDDevice=((struct FSM_SetTreeWriteElement*)fscts->Data)->id;
        FSMDevSet_fsmtset.opcode=FSM_Setting_Write;
        strcpy(FSMDevSet_fsmtset.name,((struct FSM_SetTreeWriteElement*)fscts->Data)->name);
        memcpy(FSMDevSet_fsmtset.Data,((struct FSM_SetTreeWriteElement*)fscts->Data)->Data,((struct FSM_SetTreeWriteElement*)fscts->Data)->len);
        FSMDevSet_dftv = FSM_FindDevice(FSMDevSet_fsmtset.IDDevice);
        if(FSMDevSet_dftv == 0) {
            printk(KERN_INFO "Eror \n");
            return;
        }
        FSMDevSet_dftv->TrDev->dt->Proc((char*)&FSMDevSet_fsmtset,sizeof(struct FSM_SetSetting),FSMDevSet_dftv->TrDev,FSMDevSet_dftv);
        break;
        
        case FSM_DevTreeSetReadReqest:
        FSMDevSet_fsmtget.IDDevice=((struct  FSM_SetTreeReadElement*)fscts->Data)->id;
        FSMDevSet_fsmtget.opcode=FSM_Setting_Read;
        strcpy(FSMDevSet_fsmtget.name,((struct  FSM_SetTreeReadElement *)fscts->Data)->name);
        FSMDevSet_dftv = FSM_FindDevice(FSMDevSet_fsmtset.IDDevice);
        if(FSMDevSet_dftv == 0) {
            printk(KERN_INFO "Eror \n");
            return;
        }
        FSMDevSet_dftv->TrDev->dt->Proc((char*)&FSMDevSet_fsmtset,sizeof(struct FSM_SetSetting),FSMDevSet_dftv->TrDev,FSMDevSet_dftv);
        break;
        
        case FSM_DevTreeSetReadRead:
        for(i = 0; i < FSM_DeviceSettingTreeSize; i++) 
            {
                if((FSMDevSetTree[i].id == ((unsigned short*)fscts->Data)[0]) && (FSMDevSetTree[i].reg == 1)) {
                    if(FSMDevSetTree[i].fsm_tr_size>0)
                    {
                        fscts->countparam=FSMDevSetTree[i].fsm_tr_size;
                        memcpy(fscts->Data,FSMDevSetTree[i].fsm_tr_temp,FSMDevSetTree[i].fsm_tr_size);
                    }
                    else fscts->countparam=0;
                    return;
                }
            }
        break;
        }
        break;

     
    }
}

static int __init FSMSetControlDevice_init(void)
{
    struct FSM_DeviceRegistr regp;
    FSMDevSet_dft.aplayp = 0;
    FSMDevSet_dft.type = (unsigned char)StatisticandConfig;
    FSMDevSet_dft.VidDevice = (unsigned char)FSMDeviceConfig;
    FSMDevSet_dft.PodVidDevice = (unsigned char)FSM_SettingTree_D;
    FSMDevSet_dft.KodDevice = (unsigned char)CTL_FSM_SettingTree_D;
    FSMDevSet_dft.Proc = FSM_SettingTreeControlDeviceRecive;
    FSMDevSet_dft.config_len = 0;
    FSM_DeviceClassRegister(FSMDevSet_dft);

    regp.IDDevice = FSM_TreeSettingID;
    regp.VidDevice = FSMDevSet_dft.VidDevice;
    regp.PodVidDevice = FSMDevSet_dft.PodVidDevice;
    regp.KodDevice = FSMDevSet_dft.KodDevice;
    regp.type = FSMDevSet_dft.type;
    regp.opcode = RegDevice;
    regp.CRC = 0;
    FSM_DeviceRegister(regp);
    printk(KERN_INFO "FSM Set ControlDevice loaded\n");
    return 0;
}
static void __exit FSMSetControlDevice_exit(void)
{
    FSM_ClassDeRegister(FSMDevSet_dft);
    printk(KERN_INFO "FSM Set ControlDevice module unloaded\n");
}

module_init(FSMSetControlDevice_init);
module_exit(FSMSetControlDevice_exit);