#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/fcmprotocol.h"
#include "FSM/FSMSetting/FSM_settings.h"
#include "FSM/FSMCrypto/FSM_crypt.h"

struct FSM_cryptalg FSMCA[FSM_CryptoAlgoritmNum];

int FSMCrypt_Register(FunctionEncDec Encode, FunctionEncDec Decode, char id)
{
    FSMCA[(int)id].reg = 1;
    FSMCA[(int)id].Encode = Encode;
    FSMCA[(int)id].Decode = Decode;
    return 0;
}
EXPORT_SYMBOL(FSMCrypt_Register);

int FSMCrypt_UnRegister(FunctionEncDec Encode, FunctionEncDec Decode, char id)
{
    FSMCA[(int)id].reg = 0;
    FSMCA[(int)id].Encode = 0;
    FSMCA[(int)id].Decode = 0;
    return 0;
}
EXPORT_SYMBOL(FSMCrypt_UnRegister);
void FSMEncrypt(char id, char* dst, char* src, short len)
{
    if(FSMCA[(int)id].reg == 1)
        FSMCA[(int)id].Encode(dst, src, len);
}
EXPORT_SYMBOL(FSMEncrypt);
void FSMDecrypt(char id, char* dst, char* src, short len)
{
    if(FSMCA[(int)id].reg == 1)
        FSMCA[(int)id].Decode(dst, src, len);
}
EXPORT_SYMBOL(FSMDecrypt);
void FSMSetKey(char id, void* key)
{
    if(FSMCA[(int)id].reg == 1)
        FSMCA[(int)id].Keys = key;
}
EXPORT_SYMBOL(FSMSetKey);
void* FSMGetKey(char id)
{
    if(FSMCA[(int)id].reg == 1)
        return FSMCA[(int)id].Keys;
    else
        return 0;
}
EXPORT_SYMBOL(FSMGetKey);

static int __init FSMCrypto_init(void)
{
    printk(KERN_INFO "FSMCrypto module loaded\n");
    return 0;
}

static void __exit FSMCrypto_exit(void)
{
    printk(KERN_INFO "FSMCrypto module unloaded\n");
}

module_init(FSMCrypto_init);
module_exit(FSMCrypto_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM Crypto Module");
MODULE_LICENSE("GPL");