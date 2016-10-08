#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include "FSM/FSMDevice/FSM_DeviceProcess.h"

unsigned int FSM_crc32NT(unsigned int crc, unsigned char *buf,unsigned int len) {
   unsigned int i,mask;

   crc = ~crc;
   while (len--) {
	  crc = crc ^ (*buf++); // process next byte
	  i=8;
	  while (i--) { // process 8 bits
		 mask = -(crc & 1);
		 crc = (crc >> 1) ^ (0xEDB88320 & mask);
	  }
   }
   return ~crc;
}
EXPORT_SYMBOL(FSM_crc32NT);

static int __init FSM_CRC_init(void)
{
    printk(KERN_INFO "FSM CRC loaded\n");
    return 0;
}
static void __exit FSM_CRC_exit(void)
{
    printk(KERN_INFO "FSM CRC module unloaded\n");
}
module_init(FSM_CRC_init);
module_exit(FSM_CRC_exit);

MODULE_AUTHOR("Gusenkov S.V FSM");
MODULE_DESCRIPTION("FSM CRC Module");
MODULE_LICENSE("GPL");