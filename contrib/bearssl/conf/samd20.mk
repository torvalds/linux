# Example configuration file for compiling for an Atmel SAM D20 Xplained
# Pro evaluation kit, on a Unix-like system, with a GNU toolchain.

# We are on a Unix system so we assume a Single Unix compatible 'make'
# utility, and Unix defaults.
include conf/Unix.mk

# We override the build directory.
BUILD = samd20

# C compiler, linker, and static library builder.
CC = arm-none-eabi-gcc
CFLAGS = -W -Wall -Os -mthumb -ffunction-sections -fdata-sections -mcpu=cortex-m0plus -DBR_ARMEL_CORTEXM_GCC
LD = arm-none-eabi-gcc
AR = arm-none-eabi-ar

# We compile only the static library.
DLL = no
TOOLS = no
TESTS = no
