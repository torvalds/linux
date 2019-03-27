#include "as.h"
#include "emul.h"

static const char *mipself_bfd_name PARAMS ((void));

static const char *
mipself_bfd_name ()
{
  abort ();
  return NULL;
}

#define emul_bfd_name	mipself_bfd_name
#define emul_format	&elf_format_ops

#define emul_name	"mipsbelf"
#define emul_struct_name mipsbelf
#define emul_default_endian 1
#include "emul-target.h"

#undef  emul_name
#undef  emul_struct_name
#undef  emul_default_endian

#define emul_name	"mipslelf"
#define emul_struct_name mipslelf
#define emul_default_endian 0
#include "emul-target.h"

#undef emul_name
#undef emul_struct_name
#undef emul_default_endian

#define emul_name	"mipself"
#define emul_struct_name mipself
#define emul_default_endian 2
#include "emul-target.h"
