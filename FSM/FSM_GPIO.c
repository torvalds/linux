#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i8253.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"
struct i2c_adapter* FSM_GPIO_adapter = NULL;
struct i2c_board_info FSM_GPIO_info;
struct i2c_client* FSM_GPIO_gpioclient;
struct FSM_DeviceFunctionTree FSM_GPIO_dft;
static struct timer_list FSM_GPIO_Reset_timer;
static struct timer_list FSM_GPIO_Impulse_timer;
unsigned char fsm_gpio_eror = 0;
#define GPIO_RESET_PIN FSM_GPIO_Bit_1
#define GPIO_EROR_PIN FSM_GPIO_Bit_0

void FSM_GPIO_SetBit(enum FSM_GPIO_Bit_Enum Pin)
{
    int data = 0;

#ifdef FSM_GPIO_BLOCK
    return;
#endif
    data = i2c_smbus_read_byte_data(FSM_GPIO_gpioclient, 1);
    printk(KERN_INFO "FSM GPIO Read %u \n", data);
    data |= Pin;
    i2c_smbus_write_byte_data(FSM_GPIO_gpioclient, 1, data);
    printk(KERN_INFO "FSM GPIO Write %u \n", data);
}
EXPORT_SYMBOL(FSM_GPIO_SetBit);

void FSM_GPIO_ReSetBit(enum FSM_GPIO_Bit_Enum Pin)
{

    int data = 0;

#ifdef FSM_GPIO_BLOCK
    return;
#endif

    data = i2c_smbus_read_byte_data(FSM_GPIO_gpioclient, 1);
    printk(KERN_INFO "FSM GPIO Read %u \n", data);
    data &= ~Pin;
    i2c_smbus_write_byte_data(FSM_GPIO_gpioclient, 1, data);
    printk(KERN_INFO "FSM GPIO Write %u \n", data);
}
EXPORT_SYMBOL(FSM_GPIO_ReSetBit);

void FSM_GPIO_Set_Input(enum FSM_GPIO_Bit_Enum Pin)
{
    int data = 0;

#ifdef FSM_GPIO_BLOCK
    return;
#endif

    data = i2c_smbus_read_byte_data(FSM_GPIO_gpioclient, 3);
    data |= Pin;
    i2c_smbus_write_byte_data(FSM_GPIO_gpioclient, 3, data);
}
EXPORT_SYMBOL(FSM_GPIO_Set_Input);

void FSM_GPIO_Set_Output(enum FSM_GPIO_Bit_Enum Pin)
{
    int data = 0;

#ifdef FSM_GPIO_BLOCK
    return;
#endif

    data = i2c_smbus_read_byte_data(FSM_GPIO_gpioclient, 3);
    data &= ~Pin;
    i2c_smbus_write_byte_data(FSM_GPIO_gpioclient, 3, data);
}
EXPORT_SYMBOL(FSM_GPIO_Set_Output);

unsigned char FSM_GPIO_Get_Status(enum FSM_GPIO_Bit_Enum Pin)
{

    int data = 0;

#ifdef FSM_GPIO_BLOCK
    return 0;
#endif

    data = i2c_smbus_read_byte_data(FSM_GPIO_gpioclient, 3);
    data &= Pin;
    return data;
}
EXPORT_SYMBOL(FSM_GPIO_Get_Status);

void FSM_GPIO_Reset_timer_callback(unsigned long data)
{
    FSM_GPIO_ReSetBit(GPIO_RESET_PIN);
}
EXPORT_SYMBOL(FSM_GPIO_Reset_timer_callback);

void FSM_GPIO_Impulse_timer_callback(unsigned long data)
{
    if(fsm_gpio_eror)
        FSM_GPIO_ReSetBit(GPIO_EROR_PIN);
    else
        FSM_GPIO_SetBit(GPIO_EROR_PIN);
}
EXPORT_SYMBOL(FSM_GPIO_Impulse_timer_callback);

void FSM_GPIO_Reset(void)
{

#ifdef FSM_GPIO_BLOCK
    return;
#endif

    FSM_GPIO_SetBit(GPIO_RESET_PIN);
    mod_timer(&FSM_GPIO_Reset_timer, jiffies + msecs_to_jiffies(100));
}
EXPORT_SYMBOL(FSM_GPIO_Reset);

void FSM_GPIO_EventEror(void)
{

#ifdef FSM_GPIO_BLOCK
    return;
#endif

    FSM_GPIO_ReSetBit(GPIO_EROR_PIN);
    mod_timer(&FSM_GPIO_Impulse_timer, jiffies + msecs_to_jiffies(1000));
}
EXPORT_SYMBOL(FSM_GPIO_EventEror);

void FSM_GPIODeviceRecive(char* data, short len, struct FSM_DeviceTree* to_dt, struct FSM_DeviceTree* from_dt)
{
    //   struct fsm_devices_config* fsmset;
    //   struct FSM_DeviceTree* fsdt;
    // int i,j;
    //  short hlen;
    //  unsigned short tmp;
    struct FSM_SendCmdTS* fscts = (struct FSM_SendCmdTS*)data;

    switch(data[0]) {
    case RegDevice:
        FSM_Statstic_SetStatus(to_dt, "ok");
        break;
    case SendCmdToServer: ///< Отправка команды серверу
        switch(fscts->cmd) {
        }
        break;
    case PacketFromUserSpace: ///< Отправка команды серверу
        switch(fscts->cmd) {
        case FSM_OFF_Bit:
            FSM_GPIO_SetBit(fscts->Data[0]);
            break;
        case FSM_ON_Bit:
            FSM_GPIO_ReSetBit(fscts->Data[0]);
            break;
        case FSM_Reset_Bit:
            FSM_GPIO_Reset();
            break;
        case FSM_Event_Bit:
            FSM_GPIO_EventEror();
            break;
        case FSM_Eror_ON_Bit:
            FSM_GPIO_ReSetBit(GPIO_EROR_PIN);
            fsm_gpio_eror = 1;
            break;
        case FSM_Eror_OFF_Bit:
            FSM_GPIO_SetBit(GPIO_EROR_PIN);
            fsm_gpio_eror = 0;
            break;
        }
        break;
    }
}

static int __init FSM_GPIO_init(void)
{

    struct FSM_DeviceRegistr regp;

    int i = 0, found = 0;

#ifdef FSM_GPIO_BLOCK
    goto fail;
#endif

    memset(&FSM_GPIO_info, 0, sizeof(struct i2c_board_info));
    FSM_GPIO_adapter = i2c_get_adapter(i++);

    while(FSM_GPIO_adapter) {
        printk(KERN_ERR "Scan %s", FSM_GPIO_adapter->name);
        if(strncmp(FSM_GPIO_adapter->name, "SMBus", 5) == 0) {
            found = 1;
            break;
        }
        FSM_GPIO_adapter = i2c_get_adapter(i++);
    }
    if(!found)
        goto fail;

    FSM_GPIO_info.addr = 0x72;
    FSM_GPIO_info.platform_data = "fsm_pca9538";
    strncpy(FSM_GPIO_info.type, "fsm_pca9538", I2C_NAME_SIZE);
   FSM_GPIO_gpioclient = i2c_new_device(FSM_GPIO_adapter, &FSM_GPIO_info);

    if(FSM_GPIO_gpioclient == NULL) {
        printk(KERN_ERR "failed to attach GPIO\n");
        goto fail;
    }

    i2c_smbus_write_byte_data(FSM_GPIO_gpioclient, 3, 0);

    FSM_GPIO_Set_Output(FSM_GPIO_Bit_0);
    FSM_GPIO_Set_Output(FSM_GPIO_Bit_1);

    FSM_GPIO_Set_Output(FSM_GPIO_Bit_2);
    FSM_GPIO_Set_Output(FSM_GPIO_Bit_3);

    FSM_GPIO_Set_Output(FSM_GPIO_Bit_4);
    FSM_GPIO_Set_Output(FSM_GPIO_Bit_5);

    FSM_GPIO_Set_Output(FSM_GPIO_Bit_6);
    FSM_GPIO_Set_Output(FSM_GPIO_Bit_7);

    FSM_GPIO_ReSetBit(FSM_GPIO_Bit_0);
    FSM_GPIO_ReSetBit(FSM_GPIO_Bit_1);

    FSM_GPIO_ReSetBit(FSM_GPIO_Bit_2);
    FSM_GPIO_ReSetBit(FSM_GPIO_Bit_3);

    FSM_GPIO_ReSetBit(FSM_GPIO_Bit_4);
    FSM_GPIO_ReSetBit(FSM_GPIO_Bit_5);

    FSM_GPIO_ReSetBit(FSM_GPIO_Bit_6);
    FSM_GPIO_ReSetBit(FSM_GPIO_Bit_7);

    FSM_GPIO_dft.aplayp = 0;
    FSM_GPIO_dft.type = (unsigned char)ControlMachine;
    FSM_GPIO_dft.VidDevice = (unsigned char)Device;
    FSM_GPIO_dft.PodVidDevice = (unsigned char)GPIO;
    FSM_GPIO_dft.KodDevice = (unsigned char)Bit_8;
    FSM_GPIO_dft.Proc = FSM_GPIODeviceRecive;
    FSM_GPIO_dft.config_len = 0;
    FSM_DeviceClassRegister(FSM_GPIO_dft);

    regp.IDDevice = FSM_GPIOID;
    regp.VidDevice = FSM_GPIO_dft.VidDevice;
    regp.PodVidDevice = FSM_GPIO_dft.PodVidDevice;
    regp.KodDevice = FSM_GPIO_dft.KodDevice;
    regp.type = FSM_GPIO_dft.type;
    regp.opcode = RegDevice;
    regp.CRC = 0;
    FSM_DeviceRegister(regp);

    setup_timer(&FSM_GPIO_Reset_timer, FSM_GPIO_Reset_timer_callback, 0);
    setup_timer(&FSM_GPIO_Impulse_timer, FSM_GPIO_Impulse_timer_callback, 0);
    fsm_gpio_eror = 0;
    FSM_GPIO_Reset();
    FSM_GPIO_EventEror();

    printk(KERN_INFO "FSM GPIO loaded\n");
    return 0;

fail:
    printk(KERN_ERR "Fail to fount GPIO\n");
    return 0;
}

static void __exit FSM_GPIO_exit(void)
{
    FSM_ClassDeRegister(FSM_GPIO_dft);
    i2c_unregister_device(FSM_GPIO_gpioclient);
    del_timer(&FSM_GPIO_Reset_timer);
    del_timer(&FSM_GPIO_Impulse_timer);
    printk(KERN_INFO "FSM GPIO unloaded\n");
}


module_init(FSM_GPIO_init);
module_exit(FSM_GPIO_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM GPIO  Module");
MODULE_LICENSE("GPL");