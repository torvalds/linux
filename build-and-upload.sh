#!/bin/bash
set -euo pipefail

# Configuration
S3_BUCKET="unvariance-kernel-dev"
S3_REGION="us-east-2"
BUILD_ID="${BUILD_ID:-$(git rev-parse HEAD)}"
# Dynamically determine kernel version (including git state and LOCALVERSION)
KERNEL_VERSION=$(make kernelrelease)
CC="ccache gcc"

# Create temporary directory in user home for builds
TEMP_BUILD_DIR="$HOME/.kernel-build-tmp-$$"
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
    log "Using temporary directory: $TEMP_BUILD_DIR"
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
    
    # Check if we're in the kernel source directory
    if [[ ! -f "Makefile" ]] || ! grep -q "KERNELRELEASE" Makefile; then
        error "This script must be run from the kernel source root directory."
    fi
}

# Configure kernel for our needs
configure_kernel() {
    log "Configuring kernel build..."
    
    # Start with a reasonable base config
    if [[ -f ".config" ]]; then
        log "Using existing .config"
    else
        error "No existing .config"
    fi
    
    # # Enable required features for our resctrl work
    # log "Enabling resctrl and perf features..."
    # scripts/config --enable CONFIG_X86_RESCTRL
    # scripts/config --enable CONFIG_PERF_EVENTS
    # scripts/config --enable CONFIG_X86_MSR
    # scripts/config --enable CONFIG_KEXEC
    # scripts/config --enable CONFIG_KEXEC_FILE
    # scripts/config --enable CONFIG_CRASH_DUMP
    
    # # Enable debugging features
    # scripts/config --enable CONFIG_DEBUG_KERNEL
    # scripts/config --enable CONFIG_DEBUG_INFO
    # scripts/config --enable CONFIG_DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT
    
    # # Make sure we have networking and filesystem support
    # scripts/config --enable CONFIG_NET
    # scripts/config --enable CONFIG_INET
    # scripts/config --enable CONFIG_EXT4_FS
    # scripts/config --enable CONFIG_PROC_FS
    # scripts/config --enable CONFIG_SYSFS
    
    # Update config with dependencies
    make olddefconfig
}

# Build the kernel
build_kernel() {
    log "Building kernel..."
    
    # Get number of CPU cores for parallel build
    NPROC=$(nproc)
    log "Building with ${NPROC} parallel jobs..."
    
    # Build kernel image
    make CC="$CC" -j${NPROC} bzImage
    
    log "Kernel build completed successfully"
}

# Build resctrl tests
build_tests() {
    log "Building resctrl tests..."
    
    # Check if test directory exists
    if [[ ! -d "tools/testing/selftests/resctrl" ]]; then
        error "Resctrl test directory not found"
    fi
    
    cd tools/testing/selftests/resctrl
    
    # Build the tests
    make clean
    make
    
    # Check if the test binary was created
    if [[ ! -f "resctrl_tests" ]]; then
        error "Failed to build resctrl_tests binary"
    fi
    
    # Move the binary to a temporary location for upload
    cp resctrl_tests "${TEMP_BUILD_DIR}/resctrl_tests-${KERNEL_VERSION}"
    
    cd - >/dev/null
    
    log "Resctrl tests built successfully"
}

# Build initrd using the separate script
create_initrd() {
    log "Building initrd using separate script..."
    
    if [[ ! -f "./build-initrd.sh" ]]; then
        error "build-initrd.sh not found in current directory"
    fi
    
    # Run the initrd build script with upload flag
    local initrd_output
    initrd_output=$(./build-initrd.sh --upload 2>&1) || error "Failed to build and upload initrd"
    
    # Extract SHA256 and S3 key from output
    INITRD_SHA256=$(echo "$initrd_output" | grep "INITRD_SHA256=" | cut -d'=' -f2)
    INITRD_S3_KEY=$(echo "$initrd_output" | grep "INITRD_S3_KEY=" | cut -d'=' -f2)
    
    if [[ -z "$INITRD_SHA256" || -z "$INITRD_S3_KEY" ]]; then
        error "Failed to extract initrd information from build script output"
    fi
    
    log "Initrd build completed successfully"
    log "Initrd SHA256: $INITRD_SHA256"
    log "Initrd S3 key: $INITRD_S3_KEY"
}

# Upload artifacts to S3
upload_to_s3() {
    log "Uploading kernel artifacts to S3..."
    
    local kernel_path="arch/x86/boot/bzImage"
    local test_path="${TEMP_BUILD_DIR}/resctrl_tests-${KERNEL_VERSION}"
    
    if [[ ! -f "$kernel_path" ]]; then
        error "Kernel image not found at $kernel_path"
    fi
    
    if [[ ! -f "$test_path" ]]; then
        error "Test binary not found at $test_path"
    fi
    
    # Validate initrd information from build-initrd.sh
    if [[ -z "$INITRD_SHA256" || -z "$INITRD_S3_KEY" ]]; then
        error "Initrd information not available. Make sure create_initrd() was called successfully."
    fi
    
    # Upload kernel
    local s3_kernel_key="kernels/${BUILD_ID}/bzImage"
    log "Uploading kernel to s3://${S3_BUCKET}/${s3_kernel_key}"
    aws s3 cp "$kernel_path" "s3://${S3_BUCKET}/${s3_kernel_key}" --region "$S3_REGION"
    
    # Note: initrd is already uploaded by build-initrd.sh
    log "Using pre-uploaded initrd: s3://${S3_BUCKET}/${INITRD_S3_KEY}"
    
    # Upload test binary
    local s3_test_key="kernels/${BUILD_ID}/resctrl_tests"
    log "Uploading test binary to s3://${S3_BUCKET}/${s3_test_key}"
    aws s3 cp "$test_path" "s3://${S3_BUCKET}/${s3_test_key}" --region "$S3_REGION"
    
    # Create metadata file
    local metadata_file="${TEMP_BUILD_DIR}/kernel-metadata-${BUILD_ID}.json"
    cat > "$metadata_file" << EOF
{
    "build_id": "${BUILD_ID}",
    "kernel_version": "${KERNEL_VERSION}",
    "build_date": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
    "git_commit": "$(git rev-parse HEAD)",
    "git_branch": "$(git rev-parse --abbrev-ref HEAD)",
    "kernel_path": "${s3_kernel_key}",
    "initrd_path": "${INITRD_S3_KEY}",
    "initrd_sha256": "${INITRD_SHA256}",
    "test_path": "${s3_test_key}",
    "s3_bucket": "${S3_BUCKET}",
    "s3_region": "${S3_REGION}"
}
EOF
    
    # Upload metadata
    local s3_metadata_key="kernels/${BUILD_ID}/metadata.json"
    log "Uploading metadata to s3://${S3_BUCKET}/${s3_metadata_key}"
    aws s3 cp "$metadata_file" "s3://${S3_BUCKET}/${s3_metadata_key}" --region "$S3_REGION"
    
    # Create latest pointer
    aws s3 cp "$metadata_file" "s3://${S3_BUCKET}/kernels/latest.json" --region "$S3_REGION"
    
    log "Upload completed successfully!"
    log "Kernel artifacts available at:"
    log "  bzImage:   s3://${S3_BUCKET}/${s3_kernel_key}"
    log "  initrd:    s3://${S3_BUCKET}/${INITRD_S3_KEY} (SHA256: ${INITRD_SHA256})"
    log "  test:      s3://${S3_BUCKET}/${s3_test_key}"
    log "  metadata:  s3://${S3_BUCKET}/${s3_metadata_key}"
    
    # Output for GitHub Actions (if running in GitHub Actions)
    if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
        echo "kernel_s3_key=${s3_kernel_key}" >> "$GITHUB_OUTPUT"
        echo "initrd_s3_key=${INITRD_S3_KEY}" >> "$GITHUB_OUTPUT"
        echo "initrd_sha256=${INITRD_SHA256}" >> "$GITHUB_OUTPUT"
        echo "test_s3_key=${s3_test_key}" >> "$GITHUB_OUTPUT"
        echo "metadata_s3_key=${s3_metadata_key}" >> "$GITHUB_OUTPUT"
        echo "build_id=${BUILD_ID}" >> "$GITHUB_OUTPUT"
    fi
    
    # Save build info locally
    echo "BUILD_ID=${BUILD_ID}" > .last-build-info
    echo "KERNEL_S3_KEY=${s3_kernel_key}" >> .last-build-info
    echo "INITRD_S3_KEY=${INITRD_S3_KEY}" >> .last-build-info
    echo "INITRD_SHA256=${INITRD_SHA256}" >> .last-build-info
    echo "TEST_S3_KEY=${s3_test_key}" >> .last-build-info
    echo "METADATA_S3_KEY=${s3_metadata_key}" >> .last-build-info
}

# Show usage information
usage() {
    echo "Usage: $0 [BUILD_ID]"
    echo ""
    echo "Build and upload custom kernel to S3 for testing"
    echo ""
    echo "Arguments:"
    echo "  BUILD_ID    Optional build ID (default: current git HEAD)"
    echo ""
    echo "Environment variables:"
    echo "  S3_BUCKET     S3 bucket name (default: unvariance-kernel-dev)"
    echo "  S3_REGION     S3 region (default: us-east-2)"
    echo "  FORCE_INITRD  Set to 1 to force initrd rebuild (passed to build-initrd.sh)"
    echo ""
    echo "Examples:"
    echo "  $0                      # Build with current HEAD as build ID"
    echo "  $0 abc123def            # Build with specific commit as build ID"
    echo "  BUILD_ID=test $0        # Build with custom build ID"
    echo "  FORCE_INITRD=1 $0       # Force initrd rebuild (ignores cache)"
}

# Main execution
main() {
    # Handle command line arguments
    if [[ $# -gt 1 ]]; then
        usage
        exit 1
    fi
    
    if [[ $# -eq 1 ]]; then
        if [[ "$1" == "-h" ]] || [[ "$1" == "--help" ]]; then
            usage
            exit 0
        fi
        BUILD_ID="$1"
    fi
    
    log "Starting kernel build and upload process..."
    log "Build ID: ${BUILD_ID}"
    log "Kernel Version: ${KERNEL_VERSION}"
    log "S3 Bucket: ${S3_BUCKET}"
    log "S3 Region: ${S3_REGION}"
    
    create_temp_dir
    check_dependencies
    configure_kernel
    build_kernel
    build_tests
    create_initrd
    upload_to_s3
    
    log "Kernel build and upload completed successfully!"
    log "To test this kernel, run: ./trigger-kernel-test.sh -b ${BUILD_ID}"
}

# Run if executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi