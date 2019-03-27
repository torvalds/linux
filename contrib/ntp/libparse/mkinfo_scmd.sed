1i\
/*\
\ * Automatically generated - do not modify\
\ */\
\
#include <config.h>\
#include "ntp_types.h"\
#include "ntpd.h"\
#include "trimble.h"\
\
cmd_info_t trimble_scmds[] = {
s!^#define[ 	][ 	]*\(CMD_C[^ 	]*\)[ 	][ 	]*\([^ 	]*\)[ 	][ 	]*/\*[ 	][ 	]*\(.*\)[ 	][ 	]*\*/!	{ \1, "\1", "\3 (\2)", "", 0 },!p
$a\
\	{ 0xFF, "", "", "", 0 }\
};
