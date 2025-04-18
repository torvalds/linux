
# BPF struct_ops for Block I/O Operations

This implementation adds BPF struct_ops support for block I/O operations, allowing BPF programs to intercept and potentially modify block I/O operations at both submission and completion points.

## Building and Testing

### Prerequisites

Ensure you have the necessary build tools installed:

```bash
sudo apt-get install build-essential libncurses-dev bison flex libssl-dev libelf-dev bc
```

```bash
   # Copy your current kernel config as a starting point
   cp /boot/config-$(uname -r) .config
   
   # Update the configuration with new options
   make olddefconfig
   
   # Enable required options
   scripts/config --enable CONFIG_BPF
   scripts/config --enable CONFIG_BPF_SYSCALL
   scripts/config --enable CONFIG_BPF_JIT
   scripts/config --enable CONFIG_DEBUG_INFO_BTF
   scripts/config --enable CONFIG_BPF_BIO_OPS
   ```
2. Create certificates
   ```bash
   sudo mkdir -p /usr/local/src/debian
   sudo apt install linux-source
   sudo cp -v /usr/src/linux-source-*/debian/canonical-*.pem /usr/local/src/debian/
   sudo apt purge linux-source*
   ```

   Update .config with
   # Certificates for signature checking
#
CONFIG_MODULE_SIG_KEY="certs/signing_key.pem"
CONFIG_MODULE_SIG_KEY_TYPE_RSA=y
CONFIG_MODULE_SIG_KEY_TYPE_ECDSA=y
CONFIG_SYSTEM_TRUSTED_KEYRING=y
CONFIG_SYSTEM_TRUSTED_KEYS="/usr/local/src/debian/canonical-certs.pem"
CONFIG_SYSTEM_EXTRA_CERTIFICATE=y
CONFIG_SYSTEM_EXTRA_CERTIFICATE_SIZE=4096
CONFIG_SECONDARY_TRUSTED_KEYRING=y
CONFIG_SYSTEM_BLACKLIST_KEYRING=y
CONFIG_SYSTEM_BLACKLIST_HASH_LIST=""
CONFIG_SYSTEM_REVOCATION_LIST=y
CONFIG_SYSTEM_REVOCATION_KEYS="/usr/local/src/debian/canonical-revoked-certs.pem"
# end of Certificates for signature checking

3. **Build the kernel**:
   ```bash
   # Build with all available cores
   make -j$(nproc)
   ```

4. **Install the kernel**:
   ```bash
   # Build and install kernel packages (Debian/Ubuntu)
   make -j$(nproc) deb-pkg
   sudo dpkg -i ../linux-image-*.deb ../linux-headers-*.deb
   
   # Or for RPM-based systems
   make -j$(nproc) rpm-pkg
   sudo rpm -ivh ../kernel-*.rpm
   
   # Or install directly
   sudo make modules_install
   sudo make install
   ```

5. **Update bootloader and reboot**:
   ```bash
   sudo update-grub  # For GRUB
   sudo bootctl update  # For systemd-boot
   sudo reboot
   ```

6. **Verify the new kernel**:
   ```bash
   uname -r
   ```
