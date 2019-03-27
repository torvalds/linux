/* Sony News running NewsOS 3.2.  */

#include <sys/param.h>
#include <machine/vmparam.h>

#define HOST_PAGE_SIZE NBPG

#define HOST_MACHINE_ARCH bfd_arch_mips

#define HOST_TEXT_START_ADDR USRTEXT
#define HOST_DATA_START_ADDR USRDATA
#define HOST_STACK_END_ADDR USRSTACK
