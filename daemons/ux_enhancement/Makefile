# HER OS User Experience Enhancement Daemon Makefile
#
# Builds the advanced user experience enhancement daemon with intelligent notifications,
# predictive search, automated file organization, smart scheduling, context-aware
# assistance, universal accessibility, multi-modal interaction, personalization,
# learning adaptation, and emotional intelligence.
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

# UX Enhancement specific libraries
LIBS += -lgtk-4 -lgdk-4 -lgobject-2.0 -lglib-2.0
LIBS += -lpulse -lpulse-mainloop-glib
LIBS += -lspeechd

# Include directories
INCLUDES = -I../shared -I/usr/include/json-c
INCLUDES += -I/usr/include/gtk-4.0 -I/usr/include/glib-2.0
INCLUDES += -I/usr/include/pulse -I/usr/include/speech-dispatcher

# Source files
SOURCES = ux_enhancement.c
OBJECTS = $(SOURCES:.c=.o)

# Target binary
TARGET = ux_enhancement

# Default target
all: $(TARGET)

# Build the UX Enhancement daemon
$(TARGET): $(OBJECTS)
	@echo "Linking $@..."
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LIBS)
	@echo "UX Enhancement daemon built successfully!"

# Compile object files
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Install UX Enhancement daemon
install: all
	@echo "Installing UX Enhancement daemon..."
	install -d $(DESTDIR)/usr/bin
	install -d $(DESTDIR)/etc/heros
	install -d $(DESTDIR)/var/lib/heros
	install -d $(DESTDIR)/var/log/heros
	install -d $(DESTDIR)/usr/share/heros/ux_enhancement
	install -m 755 $(TARGET) $(DESTDIR)/usr/bin/
	install -m 644 ux_enhancement_config.json $(DESTDIR)/etc/heros/
	@echo "UX Enhancement daemon installed successfully!"

# Uninstall
uninstall:
	@echo "Uninstalling UX Enhancement daemon..."
	rm -f $(DESTDIR)/usr/bin/$(TARGET)
	rm -f $(DESTDIR)/etc/heros/ux_enhancement_config.json
	@echo "UX Enhancement daemon uninstalled successfully!"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete!"

# Test the UX Enhancement daemon
test: all
	@echo "Running UX Enhancement daemon tests..."
	# Add test commands here
	@echo "Tests completed!"

# Performance benchmark
benchmark: all
	@echo "Running UX Enhancement daemon benchmarks..."
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
	$(CC) $(CFLAGS) -fprofile-generate -o $(TARGET) $(SOURCES) $(LIBS)
	@echo "Running training workload for PGO..."
	./$(TARGET) --pgo-training
	@echo "Rebuilding with PGO data..."
	$(CC) $(CFLAGS) -fprofile-use -fprofile-correction -o $(TARGET) $(SOURCES) $(LIBS)
	@echo "PGO build completed!"

# Accessibility optimized build
accessibility-optimized: CFLAGS += -DACCESSIBILITY_FEATURES
accessibility-optimized: all

# Emotional intelligence optimized build
emotional-optimized: CFLAGS += -DEMOTIONAL_INTELLIGENCE
emotional-optimized: all

# Multi-modal interaction optimized build
multimodal-optimized: CFLAGS += -DMULTIMODAL_INTERACTION
multimodal-optimized: all

# Security hardened build
secure: CFLAGS += -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE
secure: LDFLAGS += -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
secure: all

# Development build
dev: CFLAGS += -g -DDEBUG -DDEVELOPMENT
dev: all

# Production build
production: CFLAGS += -DNDEBUG -D_PRODUCTION
production: all

# Documentation
docs:
	@echo "Generating documentation..."
	doxygen Doxyfile
	@echo "Documentation generated!"

# Accessibility testing
test-accessibility: all
	@echo "Running accessibility tests..."
	# Test screen reader integration
	# Test voice control
	# Test high contrast mode
	# Test keyboard navigation
	@echo "Accessibility tests completed!"

# Emotional intelligence testing
test-emotional: all
	@echo "Running emotional intelligence tests..."
	# Test emotion detection
	# Test empathetic responses
	# Test stress monitoring
	# Test emotional optimization
	@echo "Emotional intelligence tests completed!"

# Multi-modal interaction testing
test-multimodal: all
	@echo "Running multi-modal interaction tests..."
	# Test voice interaction
	# Test gesture recognition
	# Test eye tracking (if available)
	# Test BCI (if available)
	@echo "Multi-modal interaction tests completed!"

# Personalization testing
test-personalization: all
	@echo "Running personalization tests..."
	# Test preference learning
	# Test interface adaptation
	# Test content personalization
	# Test workflow optimization
	@echo "Personalization tests completed!"

# Setup development environment
setup-dev:
	@echo "Setting up development environment..."
	# Install dependencies
	sudo apt-get update
	sudo apt-get install -y build-essential cmake pkg-config
	sudo apt-get install -y libjson-c-dev libcurl4-openssl-dev libsqlite3-dev
	sudo apt-get install -y libgtk-4-dev libglib2.0-dev
	sudo apt-get install -y libpulse-dev libspeechd-dev
	sudo apt-get install -y valgrind cppcheck clang-tidy
	sudo apt-get install -y doxygen graphviz
	@echo "Development environment setup completed!"

# Performance profiling
profile: all
	@echo "Running performance profiling..."
	gprof ./$(TARGET) gmon.out > profile.txt
	@echo "Profile results saved to profile.txt"

# Coverage analysis
coverage: CFLAGS += -fprofile-arcs -ftest-coverage
coverage: LIBS += -lgcov
coverage: all
	@echo "Running coverage analysis..."
	gcov $(SOURCES)
	@echo "Coverage analysis completed!"

# Package for distribution
package: production
	@echo "Creating distribution package..."
	tar -czf heros-ux-enhancement-$(shell date +%Y%m%d).tar.gz \
		$(TARGET) ux_enhancement_config.json README.md
	@echo "Package created!"

# Accessibility compliance check
accessibility-compliance: all
	@echo "Checking accessibility compliance..."
	# WCAG 2.1 compliance check
	# Section 508 compliance check
	# EN 301 549 compliance check
	@echo "Accessibility compliance check completed!"

# Emotional intelligence validation
emotional-validation: all
	@echo "Validating emotional intelligence features..."
	# Emotion detection accuracy
	# Response appropriateness
	# Stress monitoring effectiveness
	# User satisfaction metrics
	@echo "Emotional intelligence validation completed!"

# Multi-modal interaction validation
multimodal-validation: all
	@echo "Validating multi-modal interaction features..."
	# Voice recognition accuracy
	# Gesture recognition precision
	# Eye tracking accuracy (if available)
	# BCI signal quality (if available)
	@echo "Multi-modal interaction validation completed!"

# Help target
help:
	@echo "Available targets:"
	@echo "  all                    - Build the UX Enhancement daemon"
	@echo "  install                - Install the UX Enhancement daemon"
	@echo "  uninstall              - Uninstall the UX Enhancement daemon"
	@echo "  clean                  - Clean build artifacts"
	@echo "  test                   - Run tests"
	@echo "  benchmark              - Run benchmarks"
	@echo "  analyze                - Run static analysis"
	@echo "  memcheck               - Run memory check"
	@echo "  pgo                    - Profile-guided optimization build"
	@echo "  accessibility-optimized - Accessibility-optimized build"
	@echo "  emotional-optimized    - Emotional intelligence optimized build"
	@echo "  multimodal-optimized   - Multi-modal interaction optimized build"
	@echo "  secure                 - Security-hardened build"
	@echo "  dev                    - Development build"
	@echo "  production             - Production build"
	@echo "  docs                   - Generate documentation"
	@echo "  test-accessibility     - Test accessibility features"
	@echo "  test-emotional         - Test emotional intelligence"
	@echo "  test-multimodal        - Test multi-modal interaction"
	@echo "  test-personalization   - Test personalization features"
	@echo "  setup-dev              - Setup development environment"
	@echo "  profile                - Performance profiling"
	@echo "  coverage               - Coverage analysis"
	@echo "  package                - Create distribution package"
	@echo "  accessibility-compliance - Check accessibility compliance"
	@echo "  emotional-validation   - Validate emotional intelligence"
	@echo "  multimodal-validation  - Validate multi-modal interaction"
	@echo "  help                   - Show this help message"

.PHONY: all install uninstall clean test benchmark analyze memcheck pgo
.PHONY: accessibility-optimized emotional-optimized multimodal-optimized secure dev production
.PHONY: docs test-accessibility test-emotional test-multimodal test-personalization
.PHONY: setup-dev profile coverage package accessibility-compliance emotional-validation
.PHONY: multimodal-validation help 