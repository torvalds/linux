#include "../comedidev.h"
#include "comedi_fc.h"

#include "addi-data/addi_common.h"
#include "addi-data/addi_amcc_s5933.h"

#define CONFIG_APCI_3501 1

#define ADDIDATA_DRIVER_NAME	"addi_apci_3501"

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci3501.c"
#include "addi-data/addi_common.c"

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
