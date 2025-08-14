#!/bin/bash
set -euo pipefail

# Configuration
S3_BUCKET="${S3_BUCKET:-unvariance-kernel-dev}"
S3_REGION="${S3_REGION:-us-east-2}"
# Dynamically determine kernel version (including git state and LOCALVERSION)
KERNEL_VERSION=$(make kernelrelease)
CC="ccache gcc"

# Create temporary directory in user home for builds
TEMP_BUILD_DIR="$HOME/.kernel-initrd-tmp-$$"
# Cache directory for reusable initrds
INITRD_CACHE_DIR="$HOME/.kernel-initrd-cache"
trap 'cleanup_temp_dir' EXIT

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log() {
    echo -e "${GREEN}[$(date +'%Y-%m-%d %H:%M:%S')] $1${NC}"
}

warn() {
    echo -e "${YELLOW}[$(date +'%Y-%m-%d %H:%M:%S')] WARNING: $1${NC}"
}

error() {
    echo -e "${RED}[$(date +'%Y-%m-%d %H:%M:%S')] ERROR: $1${NC}"
    exit 1
}

# Cleanup function for temporary directory
cleanup_temp_dir() {
    if [[ -n "${TEMP_BUILD_DIR:-}" && -d "$TEMP_BUILD_DIR" ]]; then
        log "Cleaning up temporary directory: $TEMP_BUILD_DIR"
        rm -rf "$TEMP_BUILD_DIR"
    fi
}

# Create temporary directory
create_temp_dir() {
    mkdir -p "$TEMP_BUILD_DIR"
    mkdir -p "$INITRD_CACHE_DIR"
    log "Using temporary directory: $TEMP_BUILD_DIR"
    log "Using initrd cache directory: $INITRD_CACHE_DIR"
}

# Check dependencies
check_dependencies() {
    log "Checking dependencies..."
    
    if ! command -v aws >/dev/null 2>&1; then
        error "AWS CLI not found. Please install aws-cli."
    fi
    
    if ! command -v make >/dev/null 2>&1; then
        error "make not found. Please install build tools."
    fi
    
    if ! command -v sha256sum >/dev/null 2>&1; then
        error "sha256sum not found. Please install coreutils."
    fi
    
    # Check if we're in the kernel source directory
    if [[ ! -f "Makefile" ]] || ! grep -q "KERNELRELEASE" Makefile; then
        error "This script must be run from the kernel source root directory."
    fi
}

# Calculate SHA256 of initrd content determinants
# This includes kernel version, kernel config, and module dependencies
calculate_initrd_content_hash() {
    local hash_input_file="${TEMP_BUILD_DIR}/initrd-hash-input"
    
    log "Calculating content hash for initrd caching..." >&2
    
    # Create deterministic input for hash calculation
    {
        echo "KERNEL_VERSION=${KERNEL_VERSION}"
        # Include relevant kernel config options that affect initrd
        if [[ -f ".config" ]]; then
            grep -E "^CONFIG_(MODULES|INITRAMFS|COMPRESSION)" .config | sort
        fi
        # Include module list if modules exist
        if [[ -d "/lib/modules/${KERNEL_VERSION}" ]]; then
            find "/lib/modules/${KERNEL_VERSION}" -name "*.ko" | sort
        fi
    } > "$hash_input_file"
    
    sha256sum "$hash_input_file" | cut -d' ' -f1
}

# Create initrd using mkinitramfs (with SHA256-based caching)
create_initrd() {
    local content_hash
    content_hash=$(calculate_initrd_content_hash)
    
    # Check for cached initrd using content hash
    local cached_initrd="${INITRD_CACHE_DIR}/initrd-${content_hash}.img"
    local cached_sha256="${INITRD_CACHE_DIR}/initrd-${content_hash}.sha256"
    
    if [[ -f "$cached_initrd" && -f "$cached_sha256" && "${FORCE_INITRD:-}" != "1" ]]; then
        log "Using cached initrd: $cached_initrd"
        local initrd_sha256=$(cat "$cached_sha256")
        cp "$cached_initrd" "${TEMP_BUILD_DIR}/initrd.img"
        echo "$initrd_sha256" > "${TEMP_BUILD_DIR}/initrd.sha256"
        return 0
    fi
    
    log "Creating Ubuntu-compatible initrd using mkinitramfs..."
    
    # Check if mkinitramfs is available
    if ! command -v mkinitramfs >/dev/null 2>&1; then
        error "mkinitramfs not found. Please install initramfs-tools: apt-get install initramfs-tools"
    fi
    
    # Install kernel modules to persistent location for reuse
    PERSISTENT_MODULES="$HOME/kernel-modules"
    log "Checking kernel modules in ${PERSISTENT_MODULES}..."
    
    # Only install modules if they don't exist or if forced
    if [[ ! -d "${PERSISTENT_MODULES}/lib/modules/${KERNEL_VERSION}" || "${FORCE_INITRD:-}" == "1" ]]; then
        log "Building kernel modules..."
        NPROC=$(nproc)
        make CC="$CC" -j${NPROC} modules

        log "Installing/updating kernel modules..."
        make INSTALL_MOD_PATH="${PERSISTENT_MODULES}" modules_install
    else
        log "Reusing existing kernel modules from ${PERSISTENT_MODULES}..."
    fi
    
    # Path to our modules
    MODULES_DIR="${PERSISTENT_MODULES}/lib/modules/${KERNEL_VERSION}"
    
    if [[ ! -d "${MODULES_DIR}" ]]; then
        error "Modules directory not found: ${MODULES_DIR}"
    fi
    
    # Use mkinitramfs with system configuration and temporarily install our modules
    log "Creating initramfs using system configuration..."
    
    # Temporarily install our modules to the system location
    SYSTEM_MODULES_DIR="/lib/modules/${KERNEL_VERSION}"
    BACKUP_MODULES=""
    
    # Create /lib/modules directory if it doesn't exist
    sudo mkdir -p "/lib/modules"
    
    # Back up existing modules if they exist
    if [[ -d "${SYSTEM_MODULES_DIR}" ]]; then
        BACKUP_MODULES="${TEMP_BUILD_DIR}/backup-modules"
        log "Backing up existing modules to ${BACKUP_MODULES}..."
        sudo mv "${SYSTEM_MODULES_DIR}" "${BACKUP_MODULES}"
    fi
    
    # Symlink our modules to system location (much faster than copying)
    log "Temporarily symlinking kernel modules to system location..."
    sudo ln -sf "${MODULES_DIR}" "${SYSTEM_MODULES_DIR}"
    
    # Symlink kernel config to fix mkinitramfs warning
    BOOT_CONFIG="/boot/config-${KERNEL_VERSION}"
    BACKUP_CONFIG=""
    if [[ -f "${BOOT_CONFIG}" ]]; then
        BACKUP_CONFIG="${TEMP_BUILD_DIR}/backup-config"
        log "Backing up existing config to ${BACKUP_CONFIG}..."
        sudo mv "${BOOT_CONFIG}" "${BACKUP_CONFIG}"
    fi
    log "Temporarily symlinking kernel config to ${BOOT_CONFIG}..."
    sudo ln -sf "$(pwd)/.config" "${BOOT_CONFIG}"
    
    # Use mkinitramfs with system config directory
    log "Generating initramfs..."
    mkinitramfs -d /etc/initramfs-tools -o "${TEMP_BUILD_DIR}/initrd.img" "${KERNEL_VERSION}"
    
    # Clean up - remove our symlinks and restore backups if needed
    sudo rm -f "${SYSTEM_MODULES_DIR}"
    if [[ -n "${BACKUP_MODULES}" && -d "${BACKUP_MODULES}" ]]; then
        log "Restoring original modules..."
        sudo mv "${BACKUP_MODULES}" "${SYSTEM_MODULES_DIR}"
    fi
    
    sudo rm -f "${BOOT_CONFIG}"
    if [[ -n "${BACKUP_CONFIG}" && -f "${BACKUP_CONFIG}" ]]; then
        log "Restoring original config..."
        sudo mv "${BACKUP_CONFIG}" "${BOOT_CONFIG}"
    fi
    
    # Calculate SHA256 of the created initrd
    log "Calculating initrd SHA256..."
    local initrd_sha256
    initrd_sha256=$(sha256sum "${TEMP_BUILD_DIR}/initrd.img" | cut -d' ' -f1)
    echo "$initrd_sha256" > "${TEMP_BUILD_DIR}/initrd.sha256"
    
    # Cache the initrd and its SHA256 for future use
    log "Caching initrd for future builds..."
    cp "${TEMP_BUILD_DIR}/initrd.img" "$cached_initrd"
    echo "$initrd_sha256" > "$cached_sha256"
    
    log "Ubuntu-compatible initrd created: ${TEMP_BUILD_DIR}/initrd.img"
    log "Initrd SHA256: $initrd_sha256"
    log "Kernel modules preserved in: ${PERSISTENT_MODULES}"
}

# Upload initrd to S3 using SHA256-based path
upload_initrd_to_s3() {
    log "Uploading initrd to S3..."
    
    local initrd_path="${TEMP_BUILD_DIR}/initrd.img"
    local sha256_path="${TEMP_BUILD_DIR}/initrd.sha256"
    
    if [[ ! -f "$initrd_path" ]]; then
        error "Initrd not found at $initrd_path"
    fi
    
    if [[ ! -f "$sha256_path" ]]; then
        error "Initrd SHA256 not found at $sha256_path"
    fi
    
    local initrd_sha256
    initrd_sha256=$(cat "$sha256_path")
    
    # Check if initrd already exists in S3
    local s3_initrd_key="initrds/${initrd_sha256}/initrd.img"
    
    if aws s3 ls "s3://${S3_BUCKET}/${s3_initrd_key}" --region "$S3_REGION" >/dev/null 2>&1; then
        log "Initrd already exists in S3: s3://${S3_BUCKET}/${s3_initrd_key}"
    else
        log "Uploading initrd to s3://${S3_BUCKET}/${s3_initrd_key}"
        aws s3 cp "$initrd_path" "s3://${S3_BUCKET}/${s3_initrd_key}" --region "$S3_REGION"
        
        # Also upload the SHA256 file for verification
        local s3_sha256_key="initrds/${initrd_sha256}/initrd.sha256"
        aws s3 cp "$sha256_path" "s3://${S3_BUCKET}/${s3_sha256_key}" --region "$S3_REGION"
        
        log "Upload completed successfully!"
    fi
    
    log "Initrd available at:"
    log "  s3://${S3_BUCKET}/${s3_initrd_key}"
    log "  SHA256: ${initrd_sha256}"
    
    # Output for build scripts
    echo "INITRD_SHA256=${initrd_sha256}"
    echo "INITRD_S3_KEY=${s3_initrd_key}"
    
    # Save info locally for other scripts
    echo "INITRD_SHA256=${initrd_sha256}" > .last-initrd-info
    echo "INITRD_S3_KEY=${s3_initrd_key}" >> .last-initrd-info
    echo "KERNEL_VERSION=${KERNEL_VERSION}" >> .last-initrd-info
}

# Show usage information
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Build and upload initrd to S3 with SHA256-based caching"
    echo ""
    echo "Options:"
    echo "  -h, --help      Show this help message"
    echo "  --upload        Upload to S3 after building (default: build only)"
    echo ""
    echo "Environment variables:"
    echo "  S3_BUCKET       S3 bucket name (default: unvariance-kernel-dev)"
    echo "  S3_REGION       S3 region (default: us-east-2)"
    echo "  FORCE_INITRD    Set to 1 to force initrd rebuild (default: use cache)"
    echo ""
    echo "Examples:"
    echo "  $0                      # Build initrd locally with caching"
    echo "  $0 --upload             # Build and upload to S3"
    echo "  FORCE_INITRD=1 $0       # Force initrd rebuild (ignores cache)"
}

# Main execution
main() {
    local upload_flag=false
    
    # Handle command line arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                usage
                exit 0
                ;;
            --upload)
                upload_flag=true
                shift
                ;;
            *)
                error "Unknown option: $1"
                ;;
        esac
    done
    
    log "Starting initrd build process..."
    log "Kernel Version: ${KERNEL_VERSION}"
    if [[ "$upload_flag" == true ]]; then
        log "S3 Bucket: ${S3_BUCKET}"
        log "S3 Region: ${S3_REGION}"
    fi
    
    create_temp_dir
    check_dependencies
    create_initrd
    
    if [[ "$upload_flag" == true ]]; then
        upload_initrd_to_s3
    fi
    
    log "Initrd build completed successfully!"
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi