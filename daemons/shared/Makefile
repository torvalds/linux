# HER OS Shared Optimization Libraries Makefile
#
# Builds high-performance shared libraries for HER OS daemons
# Includes zero-copy IPC, lock-free data structures, and SIMD optimizations
#
# Author: HER OS Project
# License: GPL-2.0
# Version: 1.0.0

# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O3 -march=native -mtune=native
CFLAGS += -fPIC -fvisibility=hidden -fstack-protector-strong
CFLAGS += -D_GNU_SOURCE -D_REENTRANT -D_THREAD_SAFE

# SIMD optimizations
CFLAGS += -mavx2 -mfma -mbmi2 -march=x86-64-v3
CFLAGS += -funroll-loops -ftree-vectorize -fopt-info-vec

# Debug flags (uncomment for debugging)
# CFLAGS += -g -DDEBUG -fsanitize=address -fsanitize=undefined

# Linker flags
LDFLAGS = -shared -Wl,-soname,libheros_optimizations.so.1
LDFLAGS += -Wl,--as-needed -Wl,--gc-sections
LDFLAGS += -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack

# Libraries
LIBS = -lpthread -lrt -lm
LIBS += -lnuma

# Directories
SRCDIR = .
OBJDIR = obj
LIBDIR = lib
INCDIR = include

# Source files
SOURCES = zero_copy_ipc.c lock_free_structures.c simd_optimizations.c
OBJECTS = $(SOURCES:%.c=$(OBJDIR)/%.o)

# Library name and version
LIBNAME = libheros_optimizations
LIBVERSION = 1.0.0
LIBMAJOR = 1
LIBMINOR = 0

# Targets
TARGET = $(LIBDIR)/$(LIBNAME).so.$(LIBVERSION)
SONAME = $(LIBNAME).so.$(LIBMAJOR)
LINKNAME = $(LIBNAME).so

# Default target
all: directories $(TARGET)

# Create directories
directories:
	@mkdir -p $(OBJDIR) $(LIBDIR) $(INCDIR)

# Build shared library
$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)
	@echo "Creating symbolic links..."
	cd $(LIBDIR) && ln -sf $(LIBNAME).so.$(LIBVERSION) $(SONAME)
	cd $(LIBDIR) && ln -sf $(SONAME) $(LINKNAME)
	@echo "Library built successfully!"

# Compile object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Install library
install: all
	@echo "Installing library..."
	install -d $(DESTDIR)/usr/lib
	install -m 755 $(TARGET) $(DESTDIR)/usr/lib/
	install -m 755 $(LIBDIR)/$(SONAME) $(DESTDIR)/usr/lib/
	install -m 755 $(LIBDIR)/$(LINKNAME) $(DESTDIR)/usr/lib/
	ldconfig
	@echo "Library installed successfully!"

# Install headers
install-headers:
	@echo "Installing headers..."
	install -d $(DESTDIR)/usr/include/heros
	install -m 644 *.h $(DESTDIR)/usr/include/heros/
	@echo "Headers installed successfully!"

# Uninstall
uninstall:
	@echo "Uninstalling library..."
	rm -f $(DESTDIR)/usr/lib/$(LIBNAME).so*
	ldconfig
	rm -rf $(DESTDIR)/usr/include/heros
	@echo "Library uninstalled successfully!"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(OBJDIR) $(LIBDIR)
	@echo "Clean complete!"

# Test the library
test: all
	@echo "Running tests..."
	$(CC) $(CFLAGS) -o test_optimizations test_optimizations.c -L$(LIBDIR) -lheros_optimizations $(LIBS)
	LD_LIBRARY_PATH=$(LIBDIR):$$LD_LIBRARY_PATH ./test_optimizations
	@echo "Tests completed!"

# Performance benchmark
benchmark: all
	@echo "Running performance benchmarks..."
	$(CC) $(CFLAGS) -o benchmark_optimizations benchmark_optimizations.c -L$(LIBDIR) -lheros_optimizations $(LIBS)
	LD_LIBRARY_PATH=$(LIBDIR):$$LD_LIBRARY_PATH ./benchmark_optimizations
	@echo "Benchmarks completed!"

# Static analysis
analyze:
	@echo "Running static analysis..."
	cppcheck --enable=all --std=c11 *.c *.h
	clang-tidy *.c -checks=* -- -std=c11
	@echo "Static analysis completed!"

# Memory check
memcheck: all
	@echo "Running memory check..."
	$(CC) $(CFLAGS) -g -o test_memcheck test_optimizations.c -L$(LIBDIR) -lheros_optimizations $(LIBS)
	LD_LIBRARY_PATH=$(LIBDIR):$$LD_LIBRARY_PATH valgrind --tool=memcheck --leak-check=full ./test_memcheck
	@echo "Memory check completed!"

# Profile-guided optimization
pgo: clean
	@echo "Building with profile-guided optimization..."
	$(CC) $(CFLAGS) -fprofile-generate -o pgo_test test_optimizations.c -L$(LIBDIR) -lheros_optimizations $(LIBS)
	LD_LIBRARY_PATH=$(LIBDIR):$$LD_LIBRARY_PATH ./pgo_test
	$(CC) $(CFLAGS) -fprofile-use -fprofile-correction -o $(TARGET) $(OBJECTS) $(LIBS)
	@echo "PGO build completed!"

# Documentation
docs:
	@echo "Generating documentation..."
	doxygen Doxyfile
	@echo "Documentation generated!"

# Package for distribution
package: clean all
	@echo "Creating distribution package..."
	tar -czf heros-optimizations-$(LIBVERSION).tar.gz \
		--exclude='*.o' --exclude='*.so' --exclude='obj' --exclude='lib' \
		*.c *.h Makefile README.md LICENSE
	@echo "Package created: heros-optimizations-$(LIBVERSION).tar.gz"

# Show help
help:
	@echo "HER OS Shared Optimization Libraries Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build the shared library (default)"
	@echo "  install      - Install library to system"
	@echo "  install-headers - Install header files"
	@echo "  uninstall    - Remove library from system"
	@echo "  clean        - Remove build artifacts"
	@echo "  test         - Run unit tests"
	@echo "  benchmark    - Run performance benchmarks"
	@echo "  analyze      - Run static analysis"
	@echo "  memcheck     - Run memory leak detection"
	@echo "  pgo          - Build with profile-guided optimization"
	@echo "  docs         - Generate documentation"
	@echo "  package      - Create distribution package"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Environment variables:"
	@echo "  DESTDIR      - Installation prefix (default: /usr/local)"
	@echo "  CC           - C compiler (default: gcc)"
	@echo "  CFLAGS       - Additional compiler flags"
	@echo "  LDFLAGS      - Additional linker flags"

# Phony targets
.PHONY: all directories install install-headers uninstall clean test benchmark analyze memcheck pgo docs package help

# Dependencies
$(OBJDIR)/zero_copy_ipc.o: zero_copy_ipc.c zero_copy_ipc.h
$(OBJDIR)/lock_free_structures.o: lock_free_structures.c lock_free_structures.h
$(OBJDIR)/simd_optimizations.o: simd_optimizations.c simd_optimizations.h

# Include dependency files
-include $(OBJECTS:.o=.d)

# Generate dependency files
$(OBJDIR)/%.d: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -MM -MT $(@:.d=.o) $< > $@ 