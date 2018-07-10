# When building a library, even intra-library references,
# such as from find_applet_by_name() to applet_names[],
# don't work with -fpic on sparc, needs -fPIC.
# Don't know why it fails in this case but works when
# a binary is being built.
#
# (if is superfluous, ARCH_FPIC is only used by library build, but it
# demonstrates the point: non-pic binary does not need it)
ifeq ($(CONFIG_BUILD_LIBBUSYBOX),y)
ARCH_FPIC = -fPIC
endif
