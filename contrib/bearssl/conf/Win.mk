# Configuration for a native build on a Windows system with Visual Studio.

# Build directory.
BUILD = build

# Extension for executable files.
E = .exe

# Extension for object files.
O = .obj

# Prefix for static library file name.
LP =

# Extension for static library file name. We add an 's' so that the
# name is distinct from the 'import library' generated along with the DLL.
L = s.lib

# Prefix for DLL file name.
DP =

# Extension for DLL file name.
D = .dll

# Output file names can be overridden directly. By default, they are
# assembled using the prefix/extension macros defined above.
# BEARSSLLIB = bearssls.lib
# BEARSSLDLL = bearssl.dll
# BRSSL = brssl.exe
# TESTCRYPTO = testcrypto.exe
# TESTSPEED = testspeed.exe
# TESTX509 = testx509.exe

# File deletion tool.
RM = del /Q

# Directory creation tool.
MKDIR = mkdir

# C compiler and flags.
CC = cl
CFLAGS = -nologo -W2 -O2
CCOUT = -c -Fo

# Static library building tool.
AR = lib
ARFLAGS = -nologo
AROUT = -out:

# DLL building tool.
LDDLL = cl
LDDLLFLAGS = -nologo -LD -MT
LDDLLOUT = -Fe

# Static linker.
LD = cl
LDFLAGS = -nologo
LDOUT = -Fe

# C# compiler.
MKT0COMP = mk$PmkT0.cmd
RUNT0COMP = T0Comp.exe

# Set the values to 'no' to disable building of the corresponding element
# by default. Building can still be invoked with an explicit target call
# (e.g. 'make dll' to force build the DLL).
#STATICLIB = no
#DLL = no
#TOOLS = no
#TESTS = no
