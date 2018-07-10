###
# Makefile.basic list the most basic programs used during the build process.
# The programs listed herein is what is needed to do the basic stuff,
# such as splitting .config and fix dependency file.
# This initial step is needed to avoid files to be recompiled
# when busybox configuration changes (which is what happens when
# .config is included by main Makefile.
# ---------------------------------------------------------------------------
# fixdep: 	 Used to generate dependency information during build process
# split-include: Divide all config symbols up in a number of files in
#                include/config/...
# docproc:	 Used in Documentation/docbook

hostprogs-y	:= fixdep split-include docproc
always		:= $(hostprogs-y)

# fixdep is needed to compile other host programs
$(addprefix $(obj)/,$(filter-out fixdep,$(always))): $(obj)/fixdep
