# HER OS Desktop Applications Makefile

# Directories
CONFIGURATOR_DIR = heros_configurator
SEMANTIC_BROWSER_DIR = semantic_snapshot_browser
ATTENTION_DASHBOARD_DIR = attention_credit_dashboard

# Default target
all: configurator semantic-browser attention-dashboard

# Build HER OS Configurator
configurator:
	@echo "Building HER OS Configurator..."
	cd $(CONFIGURATOR_DIR) && make build

# Build Semantic Snapshot Browser
semantic-browser:
	@echo "Building Semantic Snapshot Browser..."
	cd $(SEMANTIC_BROWSER_DIR) && make build

# Build Attention Credit Dashboard
attention-dashboard:
	@echo "Building Attention Credit Dashboard..."
	cd $(ATTENTION_DASHBOARD_DIR) && make build

# Install all desktop applications
install: install-configurator install-semantic-browser install-attention-dashboard
	@echo "All HER OS desktop applications installed successfully!"

# Install HER OS Configurator
install-configurator: configurator
	@echo "Installing HER OS Configurator..."
	cd $(CONFIGURATOR_DIR) && sudo make install

# Install Semantic Snapshot Browser
install-semantic-browser: semantic-browser
	@echo "Installing Semantic Snapshot Browser..."
	cd $(SEMANTIC_BROWSER_DIR) && sudo make install

# Install Attention Credit Dashboard
install-attention-dashboard: attention-dashboard
	@echo "Installing Attention Credit Dashboard..."
	cd $(ATTENTION_DASHBOARD_DIR) && sudo make install

# Uninstall all desktop applications
uninstall:
	@echo "Uninstalling HER OS desktop applications..."
	cd $(CONFIGURATOR_DIR) && sudo make uninstall
	cd $(SEMANTIC_BROWSER_DIR) && sudo make uninstall
	cd $(ATTENTION_DASHBOARD_DIR) && sudo make uninstall
	@echo "All HER OS desktop applications uninstalled successfully!"

# Clean all build files
clean:
	@echo "Cleaning build files..."
	cd $(CONFIGURATOR_DIR) && make clean
	cd $(SEMANTIC_BROWSER_DIR) && make clean
	cd $(ATTENTION_DASHBOARD_DIR) && make clean

# Check dependencies for all applications
check-deps:
	@echo "Checking dependencies for all desktop applications..."
	cd $(CONFIGURATOR_DIR) && make check-deps
	cd $(SEMANTIC_BROWSER_DIR) && make check-deps
	cd $(ATTENTION_DASHBOARD_DIR) && make check-deps

# Build with dependency check
build: check-deps all

# Run applications (for testing)
run-configurator: configurator
	cd $(CONFIGURATOR_DIR) && make run

run-semantic-browser: semantic-browser
	cd $(SEMANTIC_BROWSER_DIR) && make run

run-attention-dashboard: attention-dashboard
	cd $(ATTENTION_DASHBOARD_DIR) && make run

# Debug builds
debug: debug-configurator debug-semantic-browser debug-attention-dashboard

debug-configurator:
	cd $(CONFIGURATOR_DIR) && make debug

debug-semantic-browser:
	cd $(SEMANTIC_BROWSER_DIR) && make debug

debug-attention-dashboard:
	cd $(ATTENTION_DASHBOARD_DIR) && make debug

# Release builds
release: release-configurator release-semantic-browser release-attention-dashboard

release-configurator:
	cd $(CONFIGURATOR_DIR) && make release

release-semantic-browser:
	cd $(SEMANTIC_BROWSER_DIR) && make release

release-attention-dashboard:
	cd $(ATTENTION_DASHBOARD_DIR) && make release

# Package all applications
package: package-configurator package-semantic-browser package-attention-dashboard
	@echo "Creating complete desktop package..."
	mkdir -p heros-desktop-package
	cp $(CONFIGURATOR_DIR)/heros-configurator.tar.gz heros-desktop-package/ 2>/dev/null || true
	cp $(SEMANTIC_BROWSER_DIR)/heros-semantic-browser.tar.gz heros-desktop-package/ 2>/dev/null || true
	cp $(ATTENTION_DASHBOARD_DIR)/heros-attention-dashboard.tar.gz heros-desktop-package/ 2>/dev/null || true
	tar -czf heros-desktop-complete.tar.gz heros-desktop-package/
	rm -rf heros-desktop-package/
	@echo "Complete desktop package created: heros-desktop-complete.tar.gz"

package-configurator:
	cd $(CONFIGURATOR_DIR) && make package

package-semantic-browser:
	cd $(SEMANTIC_BROWSER_DIR) && make package

package-attention-dashboard:
	cd $(ATTENTION_DASHBOARD_DIR) && make package

# Quick install script
quick-install:
	@echo "Quick installation of HER OS desktop applications..."
	@echo "This will install all applications with default settings"
	curl -fsSL https://raw.githubusercontent.com/heros-os/heros/main/scripts/install_configurator.sh | bash
	@echo "Quick installation completed!"

# Help target
help:
	@echo "HER OS Desktop Applications Makefile"
	@echo "===================================="
	@echo "Available targets:"
	@echo "  all                    - Build all desktop applications"
	@echo "  configurator           - Build HER OS Configurator"
	@echo "  semantic-browser       - Build Semantic Snapshot Browser"
	@echo "  attention-dashboard    - Build Attention Credit Dashboard"
	@echo ""
	@echo "  install                - Install all applications"
	@echo "  install-configurator   - Install HER OS Configurator"
	@echo "  install-semantic-browser - Install Semantic Snapshot Browser"
	@echo "  install-attention-dashboard - Install Attention Credit Dashboard"
	@echo ""
	@echo "  uninstall              - Uninstall all applications"
	@echo "  clean                  - Clean all build files"
	@echo "  check-deps             - Check dependencies for all applications"
	@echo "  build                  - Check dependencies and build all"
	@echo ""
	@echo "  run-configurator       - Build and run HER OS Configurator"
	@echo "  run-semantic-browser   - Build and run Semantic Snapshot Browser"
	@echo "  run-attention-dashboard - Build and run Attention Credit Dashboard"
	@echo ""
	@echo "  debug                  - Build all applications with debug symbols"
	@echo "  release                - Build all applications optimized"
	@echo "  package                - Create packages for all applications"
	@echo ""
	@echo "  quick-install          - Quick installation with default settings"
	@echo "  help                   - Show this help message"
	@echo ""
	@echo "Individual application targets:"
	@echo "  debug-configurator, debug-semantic-browser, debug-attention-dashboard"
	@echo "  release-configurator, release-semantic-browser, release-attention-dashboard"
	@echo "  package-configurator, package-semantic-browser, package-attention-dashboard"

.PHONY: all configurator semantic-browser attention-dashboard install install-configurator install-semantic-browser install-attention-dashboard uninstall clean check-deps build run-configurator run-semantic-browser run-attention-dashboard debug debug-configurator debug-semantic-browser debug-attention-dashboard release release-configurator release-semantic-browser release-attention-dashboard package package-configurator package-semantic-browser package-attention-dashboard quick-install help 