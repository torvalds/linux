# Example configuration file for compiling on a Unix-like system with
# clang as compiler instead of gcc.

# We are on a Unix system so we assume a Single Unix compatible 'make'
# utility, and Unix defaults.
include conf/Unix.mk

BUILD = bclang
CC = clang
LD = clang
LDDLL = clang
