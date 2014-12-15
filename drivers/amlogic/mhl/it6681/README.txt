1.add it6681 make rule into Parent Makefile


2. add Source path into parent Kconfig

example: if dirver path is  "drivers/video/omap2/displays/it6681"

source "drivers/video/omap2/displays/it6681/Kconfig"


3.copy  merge i2c board info in machine.c to system.


4.copy it6681.h into "include/"
