
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/fcmprotocol.h"
#include "FSM/FSMDevice/fcm_audiodeviceclass.h"
#include "FSM/FSMDevice/FSM_DeviceProcess.h"
#include "FSM/FSMSetting/FSM_settings.h"
#include "FSM/FSMAudio/FSM_AudioStream.h"
#include "FSM/FSMEthernet/FSMEthernetHeader.h"

struct FSM_FIFOAS fsmfifoas[FSM_FIFOAudioStreamDeviceTreeSize];
struct FSM_SendAudioData sad;

#ifdef DEBUG_CALL_STACK
uint64_t debug_this3;
extern uint64_t debug_global;
#define DEBUG_CALL_STACK_SetStack debug_this3 = (debug_this3 << 8)
#define DEBUG_CALL_STACK_THIS 3
#define DEBUG_CALL_STACK_GLOBSET debug_global = (debug_global << 8) | (DEBUG_CALL_STACK_THIS);

typedef enum debug_function {
    init_on = 0x00,
    init_off = 0x01,
    exit_on = 0x02,
    exit_off = 0x03,
    get_astb_init = 0x04,
    get_astb_exit = 0x05,
    get_asr_init = 0x06,
    get_asr_exit = 0x07,
    get_asw_init = 0x08,
    get_asw_exit = 0x09,
    get_asre_init = 0x10,
    get_asre_exit = 0x11,

} debug_fun;
#endif

void FSM_FIFOAudioStreamTobuffer(char* Data, short len, int id)
{
    int i = 0;
    struct FSM_SendAudioData* FSMAPk = (struct FSM_SendAudioData*)Data;
    struct FSM_FIFOAS* devfifo = &fsmfifoas[FSM_AudioStreamGetFIFODevice(FSMAPk->IDDevice)];

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_GLOBSET
    DEBUG_CALL_STACK_SetStack | (get_astb_init);
#endif

    for(i = 0; i < FSMAPk->len; i++) {
        if(devfifo->in_count < 1024) {
            devfifo->inBuffer[devfifo->in_writeptr] = FSMAPk->Data[i];
            devfifo->in_writeptr++;
            if(devfifo->in_writeptr >= 1024)
                devfifo->in_writeptr = 0;
            devfifo->in_count++;
        }
    }

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_SetStack | (get_astb_exit);
#endif
}
EXPORT_SYMBOL(FSM_FIFOAudioStreamTobuffer);
int FSM_FIFOAudioStreamRegistr(struct FSM_AudioStream fsmas, unsigned short* idfifo)
{
    int fsmstream, i;

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_GLOBSET
    DEBUG_CALL_STACK_SetStack | (get_asr_init);
#endif

    fsmas.ToUser = FSM_FIFOAudioStreamTobuffer;
    fsmas.TransportDeviceType = FSM_FifoID;
    fsmstream = FSM_AudioStreamRegistr(fsmas);
    if(fsmstream != -1) {
        for(i = 0; i < FSM_FIFOAudioStreamDeviceTreeSize; i++) {
            if(fsmfifoas[i].reg == 0) {
                fsmfifoas[i].reg = 1;
                fsmfifoas[i].streamid = fsmstream;
                FSM_AudioStreamSetFIFODevice(fsmstream, i);
                *idfifo = i;
                return fsmstream;
            }
        }
    }

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_SetStack | (get_asr_exit);
#endif

    return -1;
}
EXPORT_SYMBOL(FSM_FIFOAudioStreamRegistr);
void FSM_FIFOAudioStreamWrite(char* Data, short len, unsigned short idfifo)
{
    int i = 0;

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_GLOBSET
    DEBUG_CALL_STACK_SetStack | (get_asw_init);
#endif

    if(idfifo >= FSM_FIFOAudioStreamDeviceTreeSize)
        return;
    for(i = 0; i < len; i++) {
        fsmfifoas[idfifo].outBuffer[fsmfifoas[idfifo].out_count] = Data[i];
        fsmfifoas[idfifo].out_count++;
        if(fsmfifoas[idfifo].out_count >= 160) {
            sad.IDDevice = fsmfifoas[idfifo].streamid;
            sad.len = 160;
            memcpy(sad.Data, fsmfifoas[idfifo].outBuffer, sad.len);
            FSM_AudioStreamToProcess(
                fsmfifoas[idfifo].streamid, (char*)&sad, sizeof(struct FSM_SendAudioData) - sizeof(sad.Data) + sad.len);
            fsmfifoas[idfifo].out_count -= 160;
        }
    }

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_SetStack | (get_asw_exit);
#endif
}
EXPORT_SYMBOL(FSM_FIFOAudioStreamWrite);
int FSM_FIFOAudioStreamRead(char* Data, unsigned short count, unsigned short idfifo)
{
    int i;

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_GLOBSET
    DEBUG_CALL_STACK_SetStack | (get_asre_init);
#endif

    for(i = 0; i < count; i++) {
        if(fsmfifoas[idfifo].in_count > 0) {
            Data[i] = fsmfifoas[idfifo].inBuffer[fsmfifoas[idfifo].in_readptr];
            fsmfifoas[idfifo].in_readptr++;
            if(fsmfifoas[idfifo].in_readptr >= 1024)
                fsmfifoas[idfifo].in_readptr = 0;
            fsmfifoas[idfifo].in_count--;

        } else {
            Data[i] = 0xff;
        }
    }

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_SetStack | (get_asre_exit);
#endif

    return count;
}
EXPORT_SYMBOL(FSM_FIFOAudioStreamRead);
unsigned short FSM_FIFOAudioStreamGetAS(unsigned short idfifo)
{
    return fsmfifoas[idfifo].streamid;
}
EXPORT_SYMBOL(FSM_FIFOAudioStreamGetAS);

static int __init FSM_FIFOAudioStream_init(void)
{

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_GLOBSET
    DEBUG_CALL_STACK_SetStack | (init_on);
#endif

    memset(fsmfifoas, 0, sizeof(fsmfifoas));
    sad.opcode = SendAudio;
    printk(KERN_INFO "FSM FIFO Audio Stream Module loaded\n");

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_SetStack | (init_off);
#endif
    return 0;
}

static void __exit FSM_FIFOAudioStream_exit(void)
{
#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_GLOBSET
    DEBUG_CALL_STACK_SetStack | (exit_on);
#endif

    printk(KERN_INFO "FSM FIFO Audio Stream Module unloaded\n");

#ifdef DEBUG_CALL_STACK
    DEBUG_CALL_STACK_SetStack | (exit_off);
#endif
}

module_init(FSM_FIFOAudioStream_init);
module_exit(FSM_FIFOAudioStream_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM FIFO Audio Stream Module");
MODULE_LICENSE("GPL");