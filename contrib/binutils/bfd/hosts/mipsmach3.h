#include <machine/vmparam.h>
#include <machine/machparam.h>
#include <sys/param.h>

#define	HOST_PAGE_SIZE		NBPG
/* #define	HOST_SEGMENT_SIZE	NBPG  */
#define	HOST_MACHINE_ARCH	bfd_arch_mips
#define	HOST_TEXT_START_ADDR	USRTEXT
#define	HOST_DATA_START_ADDR	USRDATA
#define	HOST_STACK_END_ADDR	USRSTACK
