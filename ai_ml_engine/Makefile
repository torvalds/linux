# Makefile for HER OS AI/ML Engine
#
# Builds the AI/ML engine that provides intelligent prediction, learning,
# and adaptation capabilities. Follows Linux kernel coding style and conventions

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2 -g
LDFLAGS = -lpthread

# Dependencies
LIBS = -lglib-2.0 -lsqlite3 -ljson-c -luuid -lcrypto -lm
PKG_CFLAGS = $(shell pkg-config --cflags glib-2.0)
PKG_LIBS = $(shell pkg-config --libs glib-2.0)

# Target binary
TARGET = ai_ml_engine
SOURCES = ai_ml_engine.c
OBJECTS = $(SOURCES:.c=.o)

# Installation paths
PREFIX = /usr
BINDIR = $(PREFIX)/bin
CONFDIR = /etc/heros
DOCDIR = $(PREFIX)/share/doc/heros-ai-ml

# Default target
all: $(TARGET)

# Build the binary
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LIBS) $(PKG_LIBS) -o $@
	@echo "Built $(TARGET) successfully"

# Compile source files
%.o: %.c ai_ml_engine.h
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -c $< -o $@

# Install the binary and configuration
install: $(TARGET)
	@echo "Installing HER OS AI/ML Engine..."
	
	# Create directories
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(CONFDIR)
	mkdir -p $(DESTDIR)$(DOCDIR)
	
	# Install binary
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/
	
	# Install configuration files
	install -m 644 ai_ml_config.json $(DESTDIR)$(CONFDIR)/ 2>/dev/null || \
		echo '{"enable_statistical_models": true, "enable_neural_networks": true, "enable_nlp": true}' > $(DESTDIR)$(CONFDIR)/ai_ml_config.json
	
	# Install documentation
	install -m 644 README.md $(DESTDIR)$(DOCDIR)/
	
	@echo "Installation complete. Use $(BINDIR)/$(TARGET) to run"

# Uninstall
uninstall:
	@echo "Uninstalling HER OS AI/ML Engine..."
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -rf $(DESTDIR)$(CONFDIR)
	rm -rf $(DESTDIR)$(DOCDIR)
	@echo "Uninstallation complete"

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete"

# Test the binary
test: $(TARGET)
	@echo "Testing AI/ML engine..."
	./$(TARGET) --help
	@echo "Test completed"

# Debug build
debug: CFLAGS += -DDEBUG -g3 -O0
debug: $(TARGET)

# Release build
release: CFLAGS += -DNDEBUG -O3
release: $(TARGET)

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@which pkg-config >/dev/null || (echo "pkg-config not found" && exit 1)
	@pkg-config --exists glib-2.0 || (echo "glib-2.0 not found" && exit 1)
	@echo "All dependencies satisfied"

# Show help
help:
	@echo "HER OS AI/ML Engine Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build the binary (default)"
	@echo "  install      - Install binary and configuration"
	@echo "  uninstall    - Remove installed files"
	@echo "  clean        - Remove build artifacts"
	@echo "  test         - Test the binary"
	@echo "  debug        - Build with debug symbols"
	@echo "  release      - Build optimized release version"
	@echo "  check-deps   - Check if all dependencies are available"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Usage:"
	@echo "  make                    # Build binary"
	@echo "  sudo make install       # Install system-wide"
	@echo "  ./ai_ml_engine          # Run binary"

# Phony targets
.PHONY: all install uninstall clean test debug release check-deps help
