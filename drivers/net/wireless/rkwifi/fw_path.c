//
// fw_path.c
//

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>

extern char fw_path[];
extern char nv_path[];

void set_firmware_path(void)
{
#ifdef CONFIG_RK903
	strcpy(fw_path, "/system/etc/firmware/fw_RK903.bin");
	strcpy(nv_path, "/system/etc/firmware/nvram_RK903.txt");
	return;
#endif	

#ifdef CONFIG_RK901
	strcpy(fw_path, "/system/etc/firmware/fw_RK901.bin");
	strcpy(nv_path, "/system/etc/firmware/nvram_RK901.txt");
	return;
#endif	

#ifdef CONFIG_BCM4330
	strcpy(fw_path, "/system/etc/firmware/fw_bcm4330.bin");
	strcpy(nv_path, "/system/etc/firmware/nvram_4330.txt");
	return;
#endif	
}
