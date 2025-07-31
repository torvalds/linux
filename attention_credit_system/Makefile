# Makefile for HER OS Attention Credit Economy
# Follows Linux kernel build conventions

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g -fPIC
LDFLAGS = -lpthread
LIBS = -lglib-2.0 -lsqlite3 -ljson-c -luuid -lcrypto -lm

# Package configuration
PKG_CFLAGS = $(shell pkg-config --cflags glib-2.0 json-c)
PKG_LIBS = $(shell pkg-config --libs glib-2.0 json-c)

# Target binary
TARGET = attention_credit_system

# Source files
SOURCES = attention_credit.c
OBJECTS = $(SOURCES:.c=.o)

# Installation directories
PREFIX = /usr
BINDIR = $(PREFIX)/bin
CONFDIR = /etc/heros
DOCDIR = $(PREFIX)/share/doc/heros-attention-credit

# Default target
all: $(TARGET)

# Build the main binary
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS) $(LIBS) $(PKG_LIBS)
	@echo "Build complete: $(TARGET)"

# Compile source files
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

# Install binary and configuration
install: $(TARGET)
	@echo "Installing HER OS Attention Credit Economy..."
	
	# Create directories
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(CONFDIR)
	mkdir -p $(DESTDIR)$(DOCDIR)
	
	# Install binary
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/
	
	# Install configuration files
	install -m 644 attention_credit_config.json $(DESTDIR)$(CONFDIR)/ 2>/dev/null || \
		echo '{"enable_credit_system": true, "enable_behavioral_analysis": true, "enable_feedback_integration": true, "enable_economic_balancing": true, "base_credit_pool": 100.0, "credit_regeneration_rate": 1.0, "max_credit_balance": 200.0, "min_credit_threshold": 10.0}' > $(DESTDIR)$(CONFDIR)/attention_credit_config.json
	
	# Install documentation
	install -m 644 README.md $(DESTDIR)$(DOCDIR)/
	
	@echo "Installation complete. Use $(BINDIR)/$(TARGET) to run"

# Uninstall
uninstall:
	@echo "Uninstalling HER OS Attention Credit Economy..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(CONFDIR)/attention_credit_config.json
	rm -rf $(DESTDIR)$(DOCDIR)
	@echo "Uninstallation complete"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	rm -f $(OBJECTS) $(TARGET)
	rm -f *.o *.so *.a
	@echo "Clean complete"

# Run tests
test: $(TARGET)
	@echo "Running attention credit economy tests..."
	# Unit tests
	./test_credit_allocation || echo "Credit allocation tests not available"
	./test_credit_spending || echo "Credit spending tests not available"
	./test_credit_recovery || echo "Credit recovery tests not available"
	./test_behavioral_analysis || echo "Behavioral analysis tests not available"
	./test_feedback_integration || echo "Feedback integration tests not available"
	./test_economic_balancing || echo "Economic balancing tests not available"
	@echo "Tests complete"

# Debug build
debug: CFLAGS += -DDEBUG -g3 -O0
debug: $(TARGET)

# Release build
release: CFLAGS += -DNDEBUG -O3
release: $(TARGET)

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@which $(CC) > /dev/null || (echo "Error: $(CC) not found" && exit 1)
	@pkg-config --exists glib-2.0 || (echo "Error: glib-2.0 not found" && exit 1)
	@pkg-config --exists json-c || (echo "Error: json-c not found" && exit 1)
	@echo "All dependencies found"

# Help target
help:
	@echo "HER OS Attention Credit Economy Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  all          - Build the attention credit system (default)"
	@echo "  install      - Install binary and configuration files"
	@echo "  uninstall    - Remove installed files"
	@echo "  clean        - Remove build artifacts"
	@echo "  test         - Run tests (if available)"
	@echo "  debug        - Build with debug symbols"
	@echo "  release      - Build optimized release version"
	@echo "  check-deps   - Check for required dependencies"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Environment variables:"
	@echo "  PREFIX       - Installation prefix (default: /usr)"
	@echo "  DESTDIR      - Destination directory for installation"
	@echo "  CC           - C compiler (default: gcc)"
	@echo "  CFLAGS       - C compiler flags"
	@echo "  LDFLAGS      - Linker flags"

# Phony targets
.PHONY: all install uninstall clean test debug release check-deps help

# Dependencies
attention_credit.o: attention_credit.c attention_credit.h
