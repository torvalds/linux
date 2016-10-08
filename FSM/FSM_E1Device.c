/*!
\file
\brief Драйвер E1
\authors Гусенков.С.В
\version 0.0.1_rc1
\date 30.12.2015
*/

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"

struct FSM_DeviceFunctionTree FSME1_dft;
struct FSM_E1Device FSME1Dev[FSM_E1DeviceTreeSize];
struct FSM_SendAudioData FSME1_sade1;
struct FSM_SendCmd FSME1_sendcmd;

char E1AnalisPAcket(char ch0, struct FSM_E1Device* e1dev)
{
    e1dev->pkg_count++;
    // printk( KERN_INFO "E1 Test: %u\n",ch0);
    if((ch0 & 0x40) == 0) {
        if(e1dev->bit_ch == 0) {
            //  printk( KERN_INFO "E1 Analiz: Error SH C: %i - %i -%x \n",e1dev->e1_eror_ch,e1dev->pkg_count,ch0);
            e1dev->e1_eror_ch++;
        }
        e1dev->bit_ch = 0;
        //чётный
        if((ch0 & 0x3f) == 0x1b) {
            return 0;
        } else {
            // printk( KERN_INFO "E1 Analiz: Error C \n");
            return -1;
        }
    } else {
        if(e1dev->bit_ch == 1) {
            // printk( KERN_INFO "E1 Analiz: Error SH NC: %i - %i -%x \n",e1dev->e1_eror_ch,e1dev->pkg_count,ch0);
            e1dev->e1_eror_ch++;
        }
        e1dev->bit_ch = 1;
        if((ch0 & 0x20) == 0x20) {
            printk(KERN_INFO "E1 Analiz: Error NC Avaria CS\n");
            return -2;
        }
        if((ch0 & 0x04) == 0x04) {
            // printk( KERN_INFO "E1 Analiz: Error NC Ostatohnie Zatuchania\n");
            return 0;
        }
        //нечётны
    }
    return 0;
}

void FSM_E1RecivePacket(char* data, short len)
{
    int i = 0;
    int j = 0;
    unsigned short st;
    unsigned short* E1SI;
    struct FSM_SendAudioData* FSMAPk = (struct FSM_SendAudioData*)data;
    struct FSME1Pkt* pkt = (struct FSME1Pkt*)FSMAPk->Data;
    struct FSME1Pkt* pktout = (struct FSME1Pkt*)FSME1_sade1.Data;
    char* datar = pktout->Data;
    unsigned short size = 0;
    struct FSM_E1Device* fsmdat = FSM_AudioStreamData(FSMAPk->IDDevice);

    if(fsmdat != 0) {
        E1SI = fsmdat->streams_id;
    } else {
        return;
    }

    // printk( KERN_INFO "Stream Recived %u \n",pkt->channels);

    for(i = 0; i < pkt->count; i++) {
        E1AnalisPAcket(*(pkt->Data + (pkt->channels * i)), fsmdat);
        for(j = 0; j < pkt->channels - 1; j++) {
            st = E1SI[j];
            FSM_FIFOAudioStreamWrite((pkt->Data + (pkt->channels * i) + j + 1), 1, st);
        }
    }

    FSME1_sade1.IDDevice = FSMAPk->IDDevice;
    pktout->count = pkt->count;
    pktout->channels = pkt->channels;
    for(i = 0; i < pktout->count; i++) {

        *datar = fsmdat->cht;
        fsmdat->cht++;
        datar++;
        size++;

        for(j = 0; j < pktout->channels - 1; j++) {
            if(size >= 1024)
                break;
            FSM_FIFOAudioStreamRead(datar, 1, E1SI[j]);
            datar++;
            size++;
        }
    }
    FSME1_sade1.len = size + 2;
    FSM_AudioStreamToUser(
        FSMAPk->IDDevice, (char*)&FSME1_sade1, sizeof(struct FSM_SendAudioData) - sizeof(FSME1_sade1.Data) + FSME1_sade1.len);
}

void FSM_E1SendStreaminfo(unsigned short id, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    short plen;

    memset(&FSME1_sendcmd, 0, sizeof(struct FSM_SendCmd));
    FSME1_sendcmd.opcode = SendCmdToDevice;
    FSME1_sendcmd.IDDevice = from_dt->IDDevice;
    FSME1_sendcmd.cmd = FSME1SendStream;
    FSME1_sendcmd.countparam = 1;
    ((unsigned short*)FSME1_sendcmd.Data)[0] = id;
    FSME1_sendcmd.CRC = 0;
    plen = sizeof(struct FSM_SendCmd) - sizeof(FSME1_sendcmd.Data) + 2;
    if(to_dt != 0)
        to_dt->dt->Proc((char*)&FSME1_sendcmd, plen, to_dt, from_dt);
}

void FSM_E1Recive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    int i, j;
    struct FSM_AudioStream fsmas;

    switch(data[0]) {

    case RegDevice: ///< Регистрация устройства
        FSM_Statstic_SetStatus(to_dt, "ok");
        for(i = 0; i < FSM_E1DeviceTreeSize; i++) {
            if(FSME1Dev[i].iddev == to_dt->IDDevice) {
                FSM_E1SendStreaminfo(FSME1Dev[i].idstream, from_dt, to_dt);
                FSME1Dev[i].bit_ch = 1;
                FSME1Dev[i].e1_eror_ch = 0;
                FSME1Dev[i].cht = 0;
                return;
            }
        }
        for(i = 0; i < FSM_E1DeviceTreeSize; i++) {
            if(FSME1Dev[i].reg == 0) {
                FSME1Dev[i].reg = 1;
                FSME1Dev[i].ethdev = FSM_FindEthernetDevice(to_dt->IDDevice);
                fsmas.iddev = to_dt->IDDevice;

                fsmas.ToProcess = FSM_E1RecivePacket;
                // fsmas.ToUser=FSM_E1SendPacket;
                fsmas.TransportDevice = FSME1Dev[i].ethdev->numdev;
                fsmas.TransportDeviceType = FSM_EthernetID2;
                fsmas.Data = &FSME1Dev[i];
                FSME1Dev[i].idstream = FSM_AudioStreamRegistr(fsmas);
                FSME1Dev[i].iddev = to_dt->IDDevice;
                FSME1Dev[i].bit_ch = 1;
                FSME1Dev[i].e1_eror_ch = 0;
                FSME1Dev[i].cht = 0;
                fsmas.ToProcess = 0;
                fsmas.TransportDevice = 0;
                fsmas.TransportDeviceType = FSM_FifoID;
                to_dt->data = &FSME1Dev[i];
                for(j = 0; j < 32; j++) {
                    FSM_FIFOAudioStreamRegistr(fsmas, &(FSME1Dev[i].streams_id[j]));
                    // printk( KERN_INFO "FSM %u \n",FSME1Dev[i].streams_id[j]);
                }
                FSM_E1SendStreaminfo(FSME1Dev[i].idstream, from_dt, to_dt);
                printk(KERN_INFO "FSME1 Device Added %u \n", to_dt->IDDevice);

                break;
            }
        }
        break;
    case DelLisr:
        for(i = 0; i < FSM_E1DeviceTreeSize; i++) {
            if((FSME1Dev[i].reg == 1) && (FSME1Dev[i].iddev == to_dt->IDDevice)) {

                FSM_AudioStreamUnRegistr(FSME1Dev[i].idstream);
                FSME1Dev[i].reg = 0;
                printk(KERN_INFO "FSME1 Device Deleted %u \n", to_dt->IDDevice);
                break;
            }
        }
        break;
    case AnsPing: ///< Пинг
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        break;
    case SendTxtMassage: ///< Отправка текстового сообщения
        break;
    case Alern: ///<Тревога
        break;
    case Warning: ///<Предупреждение
        break;
    case Trouble: ///<Сбой
        break;
    case Beep: ///<Звук
        break;
    default:
        break;
    }
    printk(KERN_INFO "RPack %u \n", len);
}
EXPORT_SYMBOL(FSM_E1Recive);

static int __init FSME1Protocol_init(void)
{

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_GLOBSET
    DEBUG_CALL_STACK_SetStack | (init_on);
#endif

    memset(&FSME1Dev, 0, sizeof(FSME1Dev));
    FSME1_sade1.codec = 0;
    FSME1_sade1.CRC = 0;
    FSME1_sade1.len = 160;
    FSME1_sade1.opcode = SendAudio;
    FSME1_dft.type = (unsigned char)AudioDevice;
    FSME1_dft.VidDevice = (unsigned char)CommunicationDevice;
    FSME1_dft.PodVidDevice = (unsigned char)CCK;
    FSME1_dft.KodDevice = (unsigned char)MN524;
    FSME1_dft.Proc = FSM_E1Recive;
    FSME1_dft.config_len = 0;
    FSM_DeviceClassRegister(FSME1_dft);
    // fsme1pkt2.channels=7

    printk(KERN_INFO "FSME1Protocol module loaded\n");
    return 0;
}

static void __exit FSME1Protocol_exit(void)
{
    FSM_ClassDeRegister(FSME1_dft);
    printk(KERN_INFO "FSME1Protocol module unloaded\n");
}

module_init(FSME1Protocol_init);
module_exit(FSME1Protocol_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM E1 Protocol Module");
MODULE_LICENSE("GPL");