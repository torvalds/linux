#include "as.h"
#include "emul.h"

static const char *i386elf_bfd_name PARAMS ((void));

static const char *
i386elf_bfd_name ()
{
  abort ();
  return NULL;
}

#define emul_bfd_name	i386elf_bfd_name
#define emul_format	&elf_format_ops

#define emul_name	"i386elf"
#define emul_struct_name i386elf
#define emul_default_endian 0
#include "emul-target.h"
