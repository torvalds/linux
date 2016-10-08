/*!
\file
\brief Модуль взаимодествия с Ethernet
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"

#include <linux/netfilter.h>

//struct sock* nl_sk = NULL;
struct FSM_DeviceFunctionTree FSM_Ethernet_dft;
struct FSM_DeviceTree* FSM_Ethernet_dt;
struct fsm_ethernet_dev FSM_Ethernet_fsdev[FSM_EthernetDeviceTreeSize];
struct FSM_SendCmd FSM_Ethernet_fsmsc;
struct fsm_ethernet_dev FSM_Ethernet_fsdev2;
FSM_StreamProcessSend FSM_AudioStreamCallback;

static int packet_direct_xmit(struct sk_buff* skb)
{
    struct net_device* dev = skb->dev;
    netdev_features_t features;
    struct netdev_queue* txq;
    int ret = NETDEV_TX_BUSY;

    if(unlikely(!netif_running(dev) || !netif_carrier_ok(dev)))
        goto drop;

    features = netif_skb_features(skb);
    if(skb_needs_linearize(skb, features) && __skb_linearize(skb))
        goto drop;

    txq = skb_get_tx_queue(dev, skb);

    local_bh_disable();

    HARD_TX_LOCK(dev, txq, smp_processor_id());
    if(!netif_xmit_frozen_or_drv_stopped(txq))
        ret = netdev_start_xmit(skb, dev, txq, false);
    HARD_TX_UNLOCK(dev, txq);

    local_bh_enable();

    if(!dev_xmit_complete(ret))
        kfree_skb(skb);

    return ret;
drop:
    atomic_long_inc(&dev->tx_dropped);
    kfree_skb(skb);
    return NET_XMIT_DROP;
}

unsigned int FSM_Send_Ethernet_Package(void* data, int len, struct fsm_ethernet_dev* fsmdev)
{

    struct net_device* dev;
    struct sk_buff* skb;
    int hlen;
    int tlen;

    if(fsmdev == 0)
        return 1;
    dev = fsmdev->dev;
    tlen = dev->needed_tailroom;
    hlen = LL_RESERVED_SPACE(dev);
    // skb = alloc_skb(len + hlen + tlen, GFP_ATOMIC);
    skb = dev_alloc_skb(len + hlen + tlen);
    if(skb == NULL) {
        printk(KERN_INFO "SKB Eror\n");
        goto out;
    }
    skb_reserve(skb, hlen);
    skb->dev = dev;
    // skb->protocol = fsmdev.eth.h_proto;
    skb_reset_network_header(skb);
    memcpy(skb_put(skb, len), data, len);
    if(dev_hard_header(skb, dev, __constant_htons(FSM_PROTO_ID_R), fsmdev->destmac, dev->dev_addr, skb->len) < 0)
        goto out;
    packet_direct_xmit(skb);
//надо чистить буфер
    return 0;

out:
    kfree_skb(skb);
    return 0;
}
EXPORT_SYMBOL(FSM_Send_Ethernet_Package);

unsigned int FSM_Send_Ethernet_Package2(void* data, int len, int id)
{

    return FSM_Send_Ethernet_Package(data, len, &FSM_Ethernet_fsdev[id]);
}
EXPORT_SYMBOL(FSM_Send_Ethernet_Package2);

int FSM_RegisterEthernetDevice(struct FSM_DeviceRegistr* fsmrg, struct net_device* dev, char* mac)
{
    int newr = 1;
    int i = 0;
    for(i = 0; i < FSM_EthernetDeviceTreeSize; i++) {
        if((fsmrg->IDDevice == FSM_Ethernet_fsdev[i].id) && (FSM_Ethernet_fsdev[i].reg == 1)) {
            FSM_Ethernet_fsdev[i].reg = 0;
            newr = 0;
        }
    }
    for(i = 0; i < FSM_EthernetDeviceTreeSize; i++) {
        if(FSM_Ethernet_fsdev[i].reg == 0) {
            FSM_Ethernet_fsdev[i].dev = dev;
            FSM_Ethernet_fsdev[i].id = fsmrg->IDDevice;
            FSM_Ethernet_fsdev[i].reg = 1;
            FSM_Ethernet_fsdev[i].numdev = i;
            memcpy(FSM_Ethernet_fsdev[i].destmac, mac, 6);
            if(newr)
                printk(KERN_INFO "FSM Ethernet Device Registred %u \n", fsmrg->IDDevice);
            return 0;
        }
    }

    return 2;
}
struct fsm_ethernet_dev* FSM_FindEthernetDevice(unsigned short id)
{
    int i;
    for(i = 0; i < FSM_EthernetDeviceTreeSize; i++) {
        if((id == FSM_Ethernet_fsdev[i].id) && (FSM_Ethernet_fsdev[i].reg == 1))
            return &FSM_Ethernet_fsdev[i];
    }

    return 0;
}
EXPORT_SYMBOL(FSM_FindEthernetDevice);

int FSM_DeleteEthernetDevice(unsigned short id)
{

    int i;
    for(i = 0; i < FSM_EthernetDeviceTreeSize; i++) {
        if((id == FSM_Ethernet_fsdev[i].id) && (FSM_Ethernet_fsdev[i].reg == 1)) {
            FSM_Ethernet_fsdev[i].reg = 0;
            printk(KERN_INFO "FSM Ethernet Device UnRegistred %u \n", id);
            return 0;
        }
    }

    return 1;
}
void FSM_EthernetSendPckt(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    struct fsm_ethernet_dev* fsmsd;
    struct fsm_ethernet_dev fsdev2;
    fsmsd = 0;
    if(to_dt != 0)
        fsmsd = FSM_FindEthernetDevice(from_dt->IDDevice);
    else
        fsmsd = 0;

    if(fsmsd == 0) {
        fsdev2.numdev = 1;
        memset(fsdev2.destmac, 0xFF, 6);
        fsdev2.dev = first_net_device(&init_net);
        while(fsdev2.dev) {
            FSM_Send_Ethernet_Package(data, len, &fsdev2);
            fsdev2.dev = next_net_device(fsdev2.dev);
        }
        // printk(KERN_INFO "FSM Ethernet Not Registred. Use Broacast \n");
        return;
    }
    //  printk( KERN_INFO "FSM Send %u \n",len);

    FSM_Send_Ethernet_Package(data, len, fsmsd);
}

void FSM_RegisterAudioStreamCallback(FSM_StreamProcessSend FSM_ASC)
{
    FSM_AudioStreamCallback = FSM_ASC;
}
EXPORT_SYMBOL(FSM_RegisterAudioStreamCallback);

FSM_ADSendEthPack FSM_GetAudioStreamCallback(void)
{
    return FSM_Send_Ethernet_Package2;
}
EXPORT_SYMBOL(FSM_GetAudioStreamCallback);

int
FSMClientProtocol_pack_rcv(struct sk_buff* skb, struct net_device* dev, struct packet_type* pt, struct net_device* odev)
{
    struct FSM_DeviceTree* dftv;

    char dats = ((char*)skb->data)[0];
    struct ethhdr* eth = eth_hdr(skb);
    if(skb->pkt_type == PACKET_OTHERHOST || skb->pkt_type == PACKET_LOOPBACK)
        goto clear;

    // printk( KERN_ERR "RegDev %u\n",dats);
    switch(dats) {
    case RegDevice: ///< Регистрация устройства

        if(FSM_RegisterEthernetDevice((struct FSM_DeviceRegistr*)skb->data, dev, eth->h_source) == 0) {
            if(FSM_DeviceRegister(*((struct FSM_DeviceRegistr*)skb->data)) != 0)
                goto clear;
        }
        dftv = FSM_FindDevice(((struct FSM_DeviceRegistr*)skb->data)->IDDevice);
        if(dftv == 0) {
            printk(KERN_INFO "Eror \n");
            goto clear;
        }
        // printk( KERN_INFO "FSM Dev %u\n",((struct FSM_SendCmdTS *)skb->data)->IDDevice);
        dftv->TrDev = FSM_Ethernet_dt;
        dftv->dt->Proc((char*)skb->data, sizeof(struct FSM_DeviceRegistr), dftv, FSM_Ethernet_dt);
        ((struct FSM_DeviceRegistr*)skb->data)->opcode = AnsRegDevice;
        FSM_Send_Ethernet_Package(skb->data,
                                  sizeof(struct FSM_DeviceRegistr),
                                  FSM_FindEthernetDevice(((struct FSM_DeviceRegistr*)skb->data)->IDDevice));

        break;
    case AnsRegDevice: ///< Подтверждение регистрации
        break;
    case DelLisr: ///< Удаление устройства из списка
        dftv = FSM_FindDevice(((struct FSM_DeviceDelete*)skb->data)->IDDevice);
        if(dftv == 0) {
            printk(KERN_INFO "Eror \n");
            goto clear;
        }
        // printk( KERN_INFO "FSM Dev %u\n",((struct FSM_SendCmdTS *)skb->data)->IDDevice);
        dftv->dt->Proc((char*)skb->data, sizeof(struct FSM_DeviceDelete), dftv, FSM_Ethernet_dt);
        FSM_DeRegister(*((struct FSM_DeviceDelete*)skb->data));
        ((struct FSM_DeviceDelete*)skb->data)->opcode = AnsDelList;
        FSM_Send_Ethernet_Package(skb->data,
                                  sizeof(struct FSM_DeviceDelete),
                                  FSM_FindEthernetDevice(((struct FSM_DeviceDelete*)skb->data)->IDDevice));
        FSM_DeleteEthernetDevice(((struct FSM_DeviceDelete*)skb->data)->IDDevice);

        break;
    case AnsDelList: ///< Подтверждение удаления устройства из списка
        break;
    case AnsPing:
        dftv = FSM_FindDevice(((struct FSM_Ping*)skb->data)->IDDevice);
        if(dftv == 0) {
            printk(KERN_INFO "Eror \n");
            goto clear;
        }
        dftv->dt->Proc((char*)skb->data, sizeof(struct FSM_Ping), dftv, FSM_Ethernet_dt);
        break;
    case FSMPing: ///< Пинг
        ((struct FSM_Ping*)skb->data)->opcode = AnsPing;
        FSM_Send_Ethernet_Package(
            skb->data, sizeof(struct FSM_Ping*), FSM_FindEthernetDevice(((struct FSM_Ping*)skb->data)->IDDevice));
        break;
    case SendCmdToDevice: ///< Отправка команды устройству
        goto clear;
        break;
    case AnsSendCmdToDevice: ///< Подтверждение приёма команды устройством
        break;
    case RqToDevice: ///< Ответ на команду устройством
        break;
    case AnsRqToDevice: ///< Подтверждение приёма команды сервером
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        dftv = FSM_FindDevice(((struct FSM_SendCmdTS*)skb->data)->IDDevice);
        if(dftv == 0) {
            printk(KERN_INFO "Eror \n");
            FSM_Ethernet_fsmsc.cmd = FSMNotRegistred;
            FSM_Ethernet_fsmsc.CRC = 0;
            FSM_Ethernet_fsmsc.countparam = 0;
            FSM_Ethernet_fsmsc.IDDevice = ((struct FSM_SendCmdTS*)skb->data)->IDDevice;
            FSM_Ethernet_fsmsc.opcode = SendCmdGlobalcmdToClient;
            memset(FSM_Ethernet_fsdev2.destmac, 0xFF, 6);
            FSM_Ethernet_fsdev2.id = ((struct FSM_SendCmdTS*)skb->data)->IDDevice;
            FSM_Ethernet_fsdev2.numdev = 1;
            FSM_Ethernet_fsdev2.dev = dev;
            FSM_Send_Ethernet_Package(&FSM_Ethernet_fsmsc, sizeof(struct FSM_SendCmd) - sizeof(FSM_Ethernet_fsmsc.Data), &FSM_Ethernet_fsdev2);

            goto clear;
        }
        // printk( KERN_INFO "FSM Dev %u\n",((struct FSM_SendCmdTS *)skb->data)->IDDevice);
        dftv->dt->Proc((char*)skb->data, skb->len, dftv, FSM_Ethernet_dt);
        ((struct FSM_SendCmdTS*)skb->data)->opcode = AnsRqToDevice;
        FSM_Send_Ethernet_Package(skb->data,
                                  FSMH_Header_Size_AnsAnsCmd,
                                  FSM_FindEthernetDevice(((struct FSM_SendCmdTS*)skb->data)->IDDevice));
        break;
    case SendTxtMassage: ///< Отправка текстового сообщения
        dftv = FSM_FindDevice(((struct FSM_Header*)skb->data)->IDDevice);
        if(dftv == 0) {
            goto clear;
        }
        dftv->dt->Proc((char*)skb->data, skb->len, dftv, FSM_Ethernet_dt);
        goto clear;
        break;
    case AnsSendTxtMassage: ///< Подтверждение приёма текстового сообщения
        break;
    case SendTxtEncMassage: ///< Отправка зашифрованного текстового сообщения
        break;
    case AnsSendTxtEncMassage: ///< Подтверждение приёма зашифрованного текстового сообщения
        break;
    case SendAudio: ///< Передача аудио данных
        // printk( KERN_INFO "FSM ID %u\n",((struct FSM_SendAudioData*)skb->data)->IDDevice);
        if((FSM_AudioStreamCallback != 0) &&
           (((struct FSM_SendAudioData*)skb->data)->IDDevice < FSM_AudioStreamDeviceTreeSize))
            FSM_AudioStreamCallback(((struct FSM_SendAudioData*)skb->data)->IDDevice, skb->data, skb->len);
        goto clear;
        break;
    case SendVideo: ///< Передача видео данных
        break;
    case SendBinData: ///< Передача бинарных данных
        break;
    case AnsSendBinData: ///< Подтверждение приёма бинарных данных
        break;
    case SendSMS: ///< Отправить СМС
        break;
    case SendAnsSMS: ///< Подтверждение СМС
        break;
    case SendSMStoDev: ///< Передача СМС устройству
        break;
    case SendAnsSMStoDev: ///< Подтверждение СМС устройством
        break;
    case SendEncSMS: ///< Отправить зашифрованного СМС
        break;
    case SendAnsEncSMS: ///<Подтверждение зашифрованного СМС
        break;
    case SendEncSMStoDev: ///< Отправить зашифрованного СМС устройству
        break;
    case SendAnsEncSMStoDev: ///< Подтверждение зашифрованного СМС  устройства
        break;
    case SendEmail: ///< Отправка email
        break;
    case AnsEmail: ///<Подтверждение email
        break;
    case SendEmailtoDevice: ///<Передача email устройству
        break;
    case AnsSendEmailtoDevice: ///<Подтверждение email устройством
        break;
    case SendEncEmail: ///<Отправить зашифрованного email
        break;
    case AnsEncEmail: ///<Подтверждение зашифрованного email
        break;
    case SendEncEmailtoDev: ///< Отправить зашифрованного email устройству
        break;
    case AnsEncEmailtoDev: ///< Подтверждение зашифрованного email   устройства
        break;
    case SocSend: ///< Отправка сообщение в социальную сеть
        break;
    case AnsSocSend: ///< Подтверждение сообщения в социальную сеть
        break;
    case SocSendtoDev: ///< Передача сообщения в социальную сеть устройству
        break;
    case AnsSocSendtoDev: ///< Подтверждение   сообщения в социальную сеть устройством
        break;
    case SocEncSend: ///< Отправить зашифрованного сообщения в социальную сеть
        break;
    case AnsSocEncSend: ///< Подтверждение зашифрованного сообщения в социальную сеть
        break;
    case SocEncSendtoDev: ///<	Отправить зашифрованного сообщения в социальную сеть устройству
        break;
    case AnsSocEncSendtoDev: ///<	Подтверждение зашифрованного сообщения в социальную сеть   устройства
        break;
    case Alern: ///<Тревога
        printk(KERN_ALERT "%u:Alerm\n", ((struct FSM_Header*)(skb->data))->IDDevice);
        FSM_GPIO_EventEror();
        dftv = FSM_FindDevice(((struct FSM_Header*)skb->data)->IDDevice);
        if(dftv == 0) {
            printk(KERN_INFO "Eror \n");
            goto clear;
        }
        // printk( KERN_INFO "FSM Dev %u\n",((struct FSM_SendCmdTS *)skb->data)->IDDevice);
        dftv->dt->Proc((char*)skb->data, FSMH_Header_Size_AlernSignal, dftv, FSM_Ethernet_dt);
        break;
    case Warning: ///<Предупреждение
        // FSM_GPIO_EventEror();
        printk(KERN_WARNING "%u:Warning\n", ((struct FSM_Header*)(skb->data))->IDDevice);
        dftv = FSM_FindDevice(((struct FSM_Header*)skb->data)->IDDevice);
        if(dftv == 0) {
            printk(KERN_INFO "Eror \n");
            goto clear;
        }
        // printk( KERN_INFO "FSM Dev %u\n",((struct FSM_SendCmdTS *)skb->data)->IDDevice);
        dftv->dt->Proc((char*)skb->data, FSMH_Header_Size_WarningSignal, dftv, FSM_Ethernet_dt);
        break;

    case Trouble: ///<Сбой
        FSM_GPIO_EventEror();
        printk(KERN_ERR "%u:Troubles\n", ((struct FSM_Header*)(skb->data))->IDDevice);
        dftv = FSM_FindDevice(((struct FSM_Header*)skb->data)->IDDevice);
        if(dftv == 0) {
            printk(KERN_INFO "Eror \n");
            goto clear;
        }
        // printk( KERN_INFO "FSM Dev %u\n",((struct FSM_SendCmdTS *)skb->data)->IDDevice);
        dftv->dt->Proc((char*)skb->data, FSMH_Header_Size_TroubleSignal, dftv, FSM_Ethernet_dt);
        break;
    case Beep: ///<Звук
        FSM_Beep(3000, 300);
        break;
    case SendCmdGlobalcmdToServer:
        switch(((struct FSM_SendCmdTS*)skb->data)->cmd) {
        case FSMGetCmdStream:
            dftv = FSM_FindDevice(((struct FSM_Header*)skb->data)->IDDevice);
            if(dftv != 0) {
                ((struct FSM_SendCmdTS*)skb->data)->opcode = SendCmdGlobalcmdToClient;
                ((struct FSM_SendCmdTS*)skb->data)->cmd = AnsFSMGetCmdStream;
                ((int*)(((struct FSM_SendCmdTS*)skb->data)->Data))[0] = FSM_ToCmdStream(dftv);
                FSM_Send_Ethernet_Package(skb->data,
                                          FSMH_Header_Size_AnsAnsCmd,
                                          FSM_FindEthernetDevice(((struct FSM_SendCmdTS*)skb->data)->IDDevice));
            }
            break;
        case FSMFlash_Start:
        case FSMFlash_Execute:
        case FSMFlash_Confirm:
        case FSMFlash_Data:
        dftv = FSM_FindDevice(((struct FSM_Header*)skb->data)->IDDevice);
        if(dftv==0) goto clear;
        FSM_FlashRecive(skb->data,sizeof(struct FSM_SendCmdTS),dftv);
        break;
        }
        break;

    case SendCmdToServerStream: ///< Отправка команды серверу
        FSM_ToProcess(((struct FSM_Header*)skb->data)->IDDevice, (char*)skb->data, skb->len, FSM_Ethernet_dt);
        break;

    case Ans_FSM_Setting_Read:
    case Ans_FSM_Setting_Write:
    case Ans_FSM_Setting_GetTree:
        dftv = FSM_FindDevice(((struct FSM_Header*)skb->data)->IDDevice);
        if(dftv == 0)
            goto clear;
        FSM_TreeRecive((char*)skb->data, skb->len, dftv);
        goto clear;
        break;
    }

/*fsdev.destmac[0]=0xa0;
 fsdev.destmac[1]=0xa1;
 fsdev.destmac[2]=0xa2;
 fsdev.destmac[3]=0xa3;
 fsdev.destmac[4]=0xa4;
 fsdev.destmac[5]=0xa5;*/

// printk( KERN_ERR "packet received with length: %u\n", skb->len );

// FSM_Send_Ethernet_Package(odev,dts,3,fsdev);

clear:
    kfree_skb(skb);
    return skb->len;
};

static struct packet_type FSMClientProtocol_proto = {
    .type = cpu_to_be16(FSM_PROTO_ID), // may be: __constant_htons( TEST_PROTO_ID ),
    .func = FSMClientProtocol_pack_rcv,
};

static int __init FSMClientProtocol_init(void)
{
    struct FSM_DeviceRegistr regp;
    dev_add_pack(&FSMClientProtocol_proto);
    FSM_Ethernet_dft.type = (unsigned char)Network;
    FSM_Ethernet_dft.VidDevice = (unsigned char)Ethernet;
    FSM_Ethernet_dft.PodVidDevice = (unsigned char)WireEthernet;
    FSM_Ethernet_dft.KodDevice = (unsigned char)StandartEthernet;
    FSM_Ethernet_dft.Proc = FSM_EthernetSendPckt;
    FSM_Ethernet_dft.config_len = 0;
    FSM_DeviceClassRegister(FSM_Ethernet_dft);

    regp.IDDevice = FSM_EthernetID2;
    regp.VidDevice = FSM_Ethernet_dft.VidDevice;
    regp.PodVidDevice = FSM_Ethernet_dft.PodVidDevice;
    regp.KodDevice = FSM_Ethernet_dft.KodDevice;
    regp.type = FSM_Ethernet_dft.type;
    regp.opcode = RegDevice;
    regp.CRC = 0;
    FSM_DeviceRegister(regp);
    FSM_Ethernet_dt = FSM_FindDevice(FSM_EthernetID2);
    FSM_Statstic_SetStatus(FSM_Ethernet_dt, "ok");
    if(FSM_Ethernet_dt == 0)
        return 1;
    memset(FSM_Ethernet_fsdev, 0, sizeof(FSM_Ethernet_fsdev));
    FSM_SendEventToAllDev(FSM_EthernetStarted);
    printk(KERN_INFO "FSMClientProtocol module loaded\n");

    return 0;
}

static void __exit FSMClientProtocol_exit(void)
{
    struct FSM_DeviceDelete delp;
    dev_remove_pack(&FSMClientProtocol_proto);
    delp.IDDevice = FSM_EthernetID2;
    delp.opcode = DelLisr;
    delp.CRC = 0;
    FSM_DeRegister(delp);
    FSM_ClassDeRegister(FSM_Ethernet_dft);
    printk(KERN_INFO "FSMClientProtocol module unloaded\n");
}

module_init(FSMClientProtocol_init);
module_exit(FSMClientProtocol_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM Protocol Module");
MODULE_LICENSE("GPL");