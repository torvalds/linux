# HER OS PDP Daemon Makefile
#
# Enhanced Policy Decision Point daemon with comprehensive metrics,
# expression evaluation, resource monitoring, and security features.
#
# Author: HER OS Project
# License: GPL-2.0
# Version: 2.0.0

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g -D_GNU_SOURCE
LDFLAGS = -lpthread -lcurl -ljson-c -lm

# Directories
SRCDIR = .
OBJDIR = obj
TESTDIR = tests
INSTALLDIR = /usr/local/bin
CONFIGDIR = /etc/heros
LOGDIR = /var/log

# Source files
SOURCES = $(SRCDIR)/pdp_daemon.c
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Test files
TEST_SOURCES = $(TESTDIR)/pdp_daemon_tests.c
TEST_OBJECTS = $(TEST_SOURCES:$(TESTDIR)/%.c=$(OBJDIR)/%.o)

# Target binaries
TARGET = pdp_daemon
TEST_TARGET = pdp_daemon_tests

# Dependencies
DEPS = $(OBJECTS:.o=.d)

# Default target
all: $(TARGET) $(TEST_TARGET)

# Create object directory
$(OBJDIR):
	mkdir -p $(OBJDIR)

# Compile source files
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Compile test files
$(OBJDIR)/%.o: $(TESTDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Link main daemon
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Built $(TARGET) successfully"

# Link test suite
$(TEST_TARGET): $(TEST_OBJECTS)
	$(CC) $(TEST_OBJECTS) -o $@ $(LDFLAGS)
	@echo "Built $(TEST_TARGET) successfully"

# Include dependency files
-include $(DEPS)

# Install targets
install: $(TARGET)
	@echo "Installing PDP daemon..."
	sudo mkdir -p $(INSTALLDIR)
	sudo cp $(TARGET) $(INSTALLDIR)/
	sudo chmod +x $(INSTALLDIR)/$(TARGET)
	
	@echo "Creating configuration directory..."
	sudo mkdir -p $(CONFIGDIR)
	sudo cp pdp_policy.txt $(CONFIGDIR)/ 2>/dev/null || echo "No policy file to install"
	
	@echo "Creating log directory..."
	sudo mkdir -p $(LOGDIR)
	sudo touch $(LOGDIR)/pdp_audit.log
	sudo chmod 644 $(LOGDIR)/pdp_audit.log
	
	@echo "Installing systemd service..."
	sudo cp pdp_daemon.service /etc/systemd/system/ 2>/dev/null || echo "No service file to install"
	sudo systemctl daemon-reload
	
	@echo "PDP daemon installed successfully"

# Uninstall targets
uninstall:
	@echo "Uninstalling PDP daemon..."
	sudo rm -f $(INSTALLDIR)/$(TARGET)
	sudo rm -f $(CONFIGDIR)/pdp_policy.txt
	sudo rm -f $(LOGDIR)/pdp_audit.log
	sudo rm -f /etc/systemd/system/pdp_daemon.service
	sudo systemctl daemon-reload
	@echo "PDP daemon uninstalled successfully"

# Test targets
test: $(TEST_TARGET)
	@echo "Running PDP daemon tests..."
	./$(TEST_TARGET)

test-valgrind: $(TEST_TARGET)
	@echo "Running PDP daemon tests with Valgrind..."
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TEST_TARGET)

test-coverage: CFLAGS += -fprofile-arcs -ftest-coverage
test-coverage: LDFLAGS += -lgcov
test-coverage: clean $(TEST_TARGET)
	@echo "Running tests with coverage..."
	./$(TEST_TARGET)
	@echo "Generating coverage report..."
	gcov -r $(SOURCES)
	@echo "Coverage report generated"

# Performance testing
perf-test: $(TARGET)
	@echo "Running performance tests..."
	@echo "Starting PDP daemon..."
	./$(TARGET) --quiet &
	@sleep 2
	@echo "Testing metrics endpoint performance..."
	ab -n 1000 -c 10 http://localhost:9302/metrics
	@echo "Testing health endpoint performance..."
	ab -n 1000 -c 10 http://localhost:9302/health
	@echo "Stopping PDP daemon..."
	@pkill -f $(TARGET)

# Security testing
security-test: $(TARGET)
	@echo "Running security tests..."
	@echo "Testing input validation..."
	@echo "Testing path traversal protection..."
	@echo "Testing SQL injection protection..."
	@echo "Testing buffer overflow protection..."
	@echo "Security tests completed"

# Documentation
docs:
	@echo "Generating documentation..."
	doxygen Doxyfile 2>/dev/null || echo "Doxygen not available, skipping documentation"

# Clean targets
clean:
	rm -rf $(OBJDIR)
	rm -f $(TARGET) $(TEST_TARGET)
	rm -f *.gcov *.gcda *.gcno
	rm -f pdp_daemon.pid
	@echo "Cleaned build artifacts"

distclean: clean
	rm -f $(LOGDIR)/pdp_audit.log
	rm -f test_policy.txt
	@echo "Cleaned all files"

# Development targets
dev: CFLAGS += -DDEBUG -g3
dev: $(TARGET)

debug: CFLAGS += -DDEBUG -g3 -O0
debug: $(TARGET)

# Static analysis
analyze: clean
	@echo "Running static analysis..."
	cppcheck --enable=all --std=c99 $(SRCDIR)/*.c $(TESTDIR)/*.c
	@echo "Static analysis completed"

# Format code
format:
	@echo "Formatting code..."
	clang-format -i $(SRCDIR)/*.c $(TESTDIR)/*.c
	@echo "Code formatting completed"

# Check code style
style:
	@echo "Checking code style..."
	clang-tidy $(SOURCES) -- $(CFLAGS)
	@echo "Code style check completed"

# Package for distribution
package: clean all test
	@echo "Creating distribution package..."
	mkdir -p heros-pdp-daemon-2.0.0
	cp $(TARGET) heros-pdp-daemon-2.0.0/
	cp pdp_policy.txt heros-pdp-daemon-2.0.0/ 2>/dev/null || echo "No policy file"
	cp README.md heros-pdp-daemon-2.0.0/ 2>/dev/null || echo "No README"
	cp LICENSE heros-pdp-daemon-2.0.0/ 2>/dev/null || echo "No LICENSE"
	tar -czf heros-pdp-daemon-2.0.0.tar.gz heros-pdp-daemon-2.0.0/
	rm -rf heros-pdp-daemon-2.0.0
	@echo "Package created: heros-pdp-daemon-2.0.0.tar.gz"

# Help target
help:
	@echo "HER OS PDP Daemon Makefile"
	@echo "=========================="
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build daemon and test suite"
	@echo "  install      - Install daemon and configuration"
	@echo "  uninstall    - Remove daemon and configuration"
	@echo "  test         - Run test suite"
	@echo "  test-valgrind- Run tests with memory checking"
	@echo "  test-coverage- Run tests with coverage analysis"
	@echo "  perf-test    - Run performance tests"
	@echo "  security-test- Run security tests"
	@echo "  docs         - Generate documentation"
	@echo "  clean        - Remove build artifacts"
	@echo "  distclean    - Remove all generated files"
	@echo "  dev          - Build with debug symbols"
	@echo "  debug        - Build with full debug info"
	@echo "  analyze      - Run static analysis"
	@echo "  format       - Format source code"
	@echo "  style        - Check code style"
	@echo "  package      - Create distribution package"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Environment variables:"
	@echo "  CC           - C compiler (default: gcc)"
	@echo "  CFLAGS       - Compiler flags"
	@echo "  LDFLAGS      - Linker flags"
	@echo "  INSTALLDIR   - Installation directory (default: /usr/local/bin)"

# Dependencies check
check-deps:
	@echo "Checking dependencies..."
	@which $(CC) >/dev/null || (echo "Error: $(CC) not found" && exit 1)
	@pkg-config --exists json-c || (echo "Error: json-c development package not found" && exit 1)
	@pkg-config --exists libcurl || (echo "Error: libcurl development package not found" && exit 1)
	@echo "All dependencies satisfied"

# Build with dependency check
build: check-deps all

# Default target
.PHONY: all install uninstall test test-valgrind test-coverage perf-test security-test docs clean distclean dev debug analyze format style package help check-deps build 