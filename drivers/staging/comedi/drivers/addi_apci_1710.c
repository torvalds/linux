#include <asm/i387.h>

#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

static void fpu_begin(void)
{
	kernel_fpu_begin();
}

static void fpu_end(void)
{
	kernel_fpu_end();
}

#define CONFIG_APCI_1710 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_1710"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_APCI1710.c"
#include "addi-data/addi_common.c"
