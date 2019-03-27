#include "as.h"
#include "emul.h"

static const char *mipsecoff_bfd_name PARAMS ((void));

static const char *
mipsecoff_bfd_name ()
{
  abort ();
  return NULL;
}

#define emul_bfd_name	mipsecoff_bfd_name
#define emul_format	&ecoff_format_ops

#define emul_name	"mipsbecoff"
#define emul_struct_name mipsbecoff
#define emul_default_endian 1
#include "emul-target.h"

#undef  emul_name
#undef  emul_struct_name
#undef  emul_default_endian

#define emul_name	"mipslecoff"
#define emul_struct_name mipslecoff
#define emul_default_endian 0
#include "emul-target.h"

#undef emul_name
#undef emul_struct_name
#undef emul_default_endian

#define emul_name	"mipsecoff"
#define emul_struct_name mipsecoff
#define emul_default_endian 2
#include "emul-target.h"
