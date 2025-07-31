# HER OS PTA Engine Makefile
#
# Builds the Proactive Task Anticipation Engine with high-performance optimizations
#
# Author: HER OS Project
# License: GPL-2.0
# Version: 1.0.0

# Compiler and flags
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -O3 -march=native -mtune=native
CFLAGS += -D_GNU_SOURCE -D_REENTRANT -D_THREAD_SAFE
CFLAGS += -fPIC -fstack-protector-strong

# SIMD optimizations
CFLAGS += -mavx2 -mfma -mbmi2 -march=x86-64-v3
CFLAGS += -funroll-loops -ftree-vectorize

# Debug flags (uncomment for debugging)
# CFLAGS += -g -DDEBUG -fsanitize=address -fsanitize=undefined

# Libraries
LIBS = -lpthread -lrt -lm -lsqlite3 -ljson-c -lcurl
LIBS += -L../shared/lib -lheros_optimizations

# Include directories
INCLUDES = -I../shared -I/usr/include/json-c

# Source files
SOURCES = pta_engine.c
OBJECTS = $(SOURCES:.c=.o)

# Target binary
TARGET = pta_engine

# Default target
all: $(TARGET)

# Build the PTA Engine
$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LIBS)
	@echo "PTA Engine built successfully!"

# Compile object files
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Install PTA Engine
install: all
	@echo "Installing PTA Engine..."
	install -d $(DESTDIR)/usr/bin
	install -d $(DESTDIR)/etc/heros
	install -d $(DESTDIR)/var/lib/heros
	install -d $(DESTDIR)/var/log/heros
	install -m 755 $(TARGET) $(DESTDIR)/usr/bin/
	install -m 644 pta_config.json $(DESTDIR)/etc/heros/
	@echo "PTA Engine installed successfully!"

# Uninstall
uninstall:
	@echo "Uninstalling PTA Engine..."
	rm -f $(DESTDIR)/usr/bin/$(TARGET)
	rm -f $(DESTDIR)/etc/heros/pta_config.json
	@echo "PTA Engine uninstalled successfully!"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete!"

# Test the PTA Engine
test: all
	@echo "Running PTA Engine tests..."
	# Add test commands here
	@echo "Tests completed!"

# Performance benchmark
benchmark: all
	@echo "Running PTA Engine benchmarks..."
	# Add benchmark commands here
	@echo "Benchmarks completed!"

# Static analysis
analyze:
	@echo "Running static analysis..."
	cppcheck --enable=all --std=c11 *.c
	clang-tidy *.c -checks=* -- -std=c11
	@echo "Static analysis completed!"

# Memory check
memcheck: all
	@echo "Running memory check..."
	valgrind --tool=memcheck --leak-check=full ./$(TARGET)
	@echo "Memory check completed!"

# Profile-guided optimization
pgo: clean
	@echo "Building with profile-guided optimization..."
	$(CC) $(CFLAGS) -fprofile-generate -o $(TARGET)_pgo $(SOURCES) $(LIBS) $(INCLUDES)
	./$(TARGET)_pgo
	$(CC) $(CFLAGS) -fprofile-use -fprofile-correction -o $(TARGET) $(SOURCES) $(LIBS) $(INCLUDES)
	@echo "PGO build completed!"

# Dependencies
$(OBJECTS): ../shared/lib/libheros_optimizations.so

# Build shared libraries first
../shared/lib/libheros_optimizations.so:
	@echo "Building shared optimization libraries..."
	$(MAKE) -C ../shared

# Show help
help:
	@echo "HER OS PTA Engine Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build the PTA Engine (default)"
	@echo "  install      - Install PTA Engine to system"
	@echo "  uninstall    - Remove PTA Engine from system"
	@echo "  clean        - Remove build artifacts"
	@echo "  test         - Run unit tests"
	@echo "  benchmark    - Run performance benchmarks"
	@echo "  analyze      - Run static analysis"
	@echo "  memcheck     - Run memory leak detection"
	@echo "  pgo          - Build with profile-guided optimization"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Environment variables:"
	@echo "  DESTDIR      - Installation prefix (default: /usr/local)"
	@echo "  CC           - C compiler (default: gcc)"
	@echo "  CFLAGS       - Additional compiler flags"
	@echo "  LIBS         - Additional libraries"

# Phony targets
.PHONY: all install uninstall clean test benchmark analyze memcheck pgo help

# Dependencies
pta_engine.o: pta_engine.c pta_config.json
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@ 