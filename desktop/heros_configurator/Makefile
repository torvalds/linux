# HER OS Configurator Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
GTK_CFLAGS = $(shell pkg-config --cflags gtk4)
GTK_LIBS = $(shell pkg-config --libs gtk4)
LIBS = $(GTK_LIBS) -lm

TARGET = heros-configurator
SOURCES = main.c
OBJECTS = $(SOURCES:.c=.o)

# Installation paths
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
DATADIR = $(PREFIX)/share
APPDIR = $(DATADIR)/applications
ICONDIR = $(DATADIR)/icons/hicolor/256x256/apps

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LIBS)

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

# Install the application
install: $(TARGET)
	@echo "Installing HER OS Configurator..."
	sudo mkdir -p $(BINDIR)
	sudo cp $(TARGET) $(BINDIR)/
	sudo chmod +x $(BINDIR)/$(TARGET)
	
	# Create desktop file
	sudo mkdir -p $(APPDIR)
	sudo tee $(APPDIR)/heros-configurator.desktop > /dev/null << 'EOF'
[Desktop Entry]
Version=1.0
Type=Application
Name=HER OS Configurator
Comment=Configure HER OS settings and AI integration
Exec=heros-configurator
Icon=heros-configurator
Terminal=false
Categories=System;Settings;Configuration;
Keywords=HER OS;Configuration;AI;Settings;
EOF

	# Create icon directory and copy icon if available
	sudo mkdir -p $(ICONDIR)
	if [ -f icon.png ]; then \
		sudo cp icon.png $(ICONDIR)/heros-configurator.png; \
	fi
	
	@echo "HER OS Configurator installed successfully!"

# Uninstall the application
uninstall:
	@echo "Uninstalling HER OS Configurator..."
	sudo rm -f $(BINDIR)/$(TARGET)
	sudo rm -f $(APPDIR)/heros-configurator.desktop
	sudo rm -f $(ICONDIR)/heros-configurator.png
	@echo "HER OS Configurator uninstalled successfully!"

# Clean build files
clean:
	rm -f $(OBJECTS) $(TARGET)

# Check dependencies
check-deps:
	@echo "Checking dependencies..."
	@if ! pkg-config --exists gtk4; then \
		echo "❌ GTK4 development libraries not found"; \
		echo "Install with: sudo apt install libgtk-4-dev"; \
		exit 1; \
	else \
		echo "✅ GTK4 development libraries found"; \
	fi
	@if ! pkg-config --exists glib-2.0; then \
		echo "❌ GLib development libraries not found"; \
		echo "Install with: sudo apt install libglib2.0-dev"; \
		exit 1; \
	else \
		echo "✅ GLib development libraries found"; \
	fi

# Build with dependency check
build: check-deps all

# Run the application
run: $(TARGET)
	./$(TARGET)

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

# Release build
release: CFLAGS += -DNDEBUG
release: clean $(TARGET)

# Package for distribution
package: release
	@echo "Creating package..."
	mkdir -p heros-configurator-package
	cp $(TARGET) heros-configurator-package/
	cp README.md heros-configurator-package/ 2>/dev/null || true
	cp LICENSE heros-configurator-package/ 2>/dev/null || true
	if [ -f icon.png ]; then \
		cp icon.png heros-configurator-package/; \
	fi
	tar -czf heros-configurator.tar.gz heros-configurator-package/
	rm -rf heros-configurator-package/
	@echo "Package created: heros-configurator.tar.gz"

# Help target
help:
	@echo "HER OS Configurator Makefile"
	@echo "============================"
	@echo "Available targets:"
	@echo "  all          - Build the application"
	@echo "  build        - Check dependencies and build"
	@echo "  install      - Install the application"
	@echo "  uninstall    - Uninstall the application"
	@echo "  clean        - Clean build files"
	@echo "  run          - Build and run the application"
	@echo "  debug        - Build with debug symbols"
	@echo "  release      - Build optimized release version"
	@echo "  package      - Create distribution package"
	@echo "  check-deps   - Check if dependencies are installed"
	@echo "  help         - Show this help message"

.PHONY: all install uninstall clean check-deps build run debug release package help 