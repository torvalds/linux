/* Definitions of target machine for GNU compiler, for MIPS running IRIX 5
   or IRIX 6 (O32 ABI) using the GNU assembler with stabs-in-mdebug.  */

/* Override iris5gas.h version again to retain mips.h default.  */
#undef MDEBUG_ASM_SPEC
#define MDEBUG_ASM_SPEC "%{!gdwarf*:-mdebug} %{gdwarf*:-no-mdebug}"
