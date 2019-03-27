CFLAGS = /decc $(CC_OPTIONS)/Diagnostics /Define=(NDEBUG) /Object=$@ /Include=([])

LINKFLAGS	= /map=$(MMS$TARGET_NAME)/cross_reference/exec=$(MMS$TARGET_NAME).exe

LINKER	      = cc

OBJS	      = closure.obj, \
		error.obj,graph.obj, \
		lalr.obj, \
		lr0.obj, \
		main.obj, \
		mkpar.obj,mstring.obj, \
		output.obj, \
		reader.obj, \
		yaccpar.obj, \
		symtab.obj, \
		verbose.obj, \
		warshall.obj

PROGRAM	      = yacc.exe

all :		$(PROGRAM)
	@ write sys$output "All done"

$(PROGRAM) :     $(OBJS)
	@ write sys$output "Loading $(PROGRAM) ... "
	@ $(LINK) $(LINKFLAGS) $(OBJS)
	@ write sys$output "done"

clean :
	@- if f$search("*.obj") .nes. "" then delete *.obj;*
	@- if f$search("*.lis") .nes. "" then delete *.lis;*
	@- if f$search("*.log") .nes. "" then delete *.log;*

clobber :	clean
	@- if f$search("*.exe") .nes. "" then delete *.exe;*

$(OBJS) : defs.h

closure.obj : closure.c
error.obj : error.c
graph.obj : graph.c
lalr.obj : lalr.c
lr0.obj : lr0.c
main.obj : main.c
mkpar.obj : mkpar.c
mstring.obj : mstring.c
output.obj : output.c
reader.obj : reader.c
yaccpar.obj : yaccpar.c
symtab.obj : symtab.c
verbose.obj : verbose.c
warshall.obj : warshall.c
