# HER OS Configurator 🎛️

A beautiful GTK4-based configuration frontend for HER OS that makes setup and configuration easy and intuitive.

## Features

### 🎯 **Easy Configuration**
- **Visual Interface** - No need to edit configuration files manually
- **Tabbed Interface** - Organized configuration sections
- **Real-time Validation** - Instant feedback on configuration changes
- **One-click Apply** - Apply changes with a single click

### 🤖 **AI Integration Setup**
- **Automatic Ollama Installation** - One-click installation of local AI
- **Cloud API Configuration** - Easy setup for Claude, Google, and OpenAI
- **Hybrid Mode** - Combine local and cloud AI for best results
- **Model Management** - Configure AI models and parameters

### ⚙️ **System Configuration**
- **Performance Tuning** - Optimize memory, CPU, and caching
- **Security Settings** - Configure audit logging, SSL, and permissions
- **Network Configuration** - Set up ports and connectivity
- **Storage Management** - Configure data and log directories

### 🔧 **Advanced Features**
- **Configuration Testing** - Validate settings before applying
- **Backup and Restore** - Save and restore configurations
- **Service Management** - Start, stop, and restart HER OS services
- **Status Monitoring** - Real-time status of all components

## Screenshots

### Main Interface
```
┌─────────────────────────────────────────────────────────────┐
│                    HER OS Configuration Center              │
│              Configure your intelligent operating system    │
├─────────────────────────────────────────────────────────────┤
│ [AI Config] [System] [Security] [Performance] [Network]    │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  AI Provider Configuration                                  │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ AI Provider: [Ollama (Local) ▼]                    │   │
│  │                                                     │   │
│  │ Ollama Installation                                  │   │
│  │ ✅ Ollama is installed and ready                    │   │
│  │ [Install Ollama]                                     │   │
│  │                                                     │   │
│  │ AI Model Configuration                               │   │
│  │ Model Name: [llama3.2:8b]                           │   │
│  │ Temperature: [██████████] 0.7                       │   │
│  │ Max Tokens: [2048]                                   │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│ [Test Config] [Save Config] [Apply Config]                 │
├─────────────────────────────────────────────────────────────┤
│ Ready to configure HER OS                                  │
└─────────────────────────────────────────────────────────────┘
```

## Installation

### Prerequisites
```bash
# Install GTK4 development libraries
sudo apt install libgtk-4-dev libglib2.0-dev pkg-config
```

### Build and Install
```bash
# Clone HER OS repository
git clone https://github.com/heros-os/heros.git
cd heros/desktop/heros_configurator

# Build the configurator
make build

# Install system-wide
sudo make install

# Launch the configurator
heros-configurator
```

### Quick Install Script
```bash
# Download and run installation script
curl -fsSL https://raw.githubusercontent.com/heros-os/heros/main/scripts/install_configurator.sh | bash
```

## Usage

### Launching the Configurator
```bash
# From applications menu
# Look for "HER OS Configurator"

# From command line
heros-configurator

# With specific configuration file
heros-configurator --config /path/to/config.conf
```

### Configuration Workflow

1. **AI Setup** - Choose your AI provider and configure models
2. **System Settings** - Configure hostname, directories, and limits
3. **Security** - Set up audit logging, SSL, and permissions
4. **Performance** - Tune caching, compression, and optimization
5. **Network** - Configure ports and connectivity
6. **API Keys** - Enter your cloud API keys (if using cloud AI)
7. **Test** - Validate your configuration
8. **Apply** - Apply changes to HER OS

### AI Provider Options

#### **Ollama (Local) - Recommended**
- ✅ **Free** - No ongoing costs
- ✅ **Private** - All data stays on your machine
- ✅ **Offline** - Works without internet
- ✅ **Fast** - Low latency responses
- ⚠️ **Limited** - Smaller models, less capable

#### **Claude API (Cloud)**
- ✅ **Powerful** - Advanced reasoning capabilities
- ✅ **Accurate** - High-quality responses
- ⚠️ **Cost** - Pay per token used
- ⚠️ **Privacy** - Data sent to Anthropic

#### **Google API (Cloud)**
- ✅ **Fast** - Quick response times
- ✅ **Cost-effective** - Lower cost than Claude
- ⚠️ **Privacy** - Data sent to Google

#### **Hybrid Mode**
- ✅ **Best of both** - Local for simple tasks, cloud for complex
- ✅ **Cost optimization** - Minimize API usage
- ✅ **Fallback** - Local models if cloud fails

## Configuration Sections

### AI Configuration
- **Provider Selection** - Choose between local and cloud AI
- **Model Configuration** - Set model name, temperature, tokens
- **Ollama Installation** - One-click installation and setup
- **API Key Management** - Secure storage of cloud API keys

### System Configuration
- **Hostname** - Set system identification
- **Directories** - Configure data and log locations
- **Resource Limits** - Set memory and CPU limits
- **Performance Settings** - Enable caching and compression

### Security Configuration
- **Audit Logging** - Enable comprehensive logging
- **SSL/TLS** - Configure secure connections
- **Firewall** - Set up network security
- **Permissions** - Configure access control

### Performance Configuration
- **Caching** - Enable and configure caching
- **Compression** - Set compression levels
- **Optimization** - Tune system performance
- **Monitoring** - Enable performance monitoring

### Network Configuration
- **Service Ports** - Configure daemon ports
- **Load Balancing** - Set up load balancer
- **SSL Certificates** - Configure SSL/TLS
- **Firewall Rules** - Set up network security

## Command Line Options

```bash
heros-configurator [OPTIONS]

Options:
  --config FILE     Load configuration from FILE
  --save FILE       Save configuration to FILE
  --test            Test current configuration
  --apply           Apply configuration immediately
  --install-ollama  Install Ollama automatically
  --help            Show this help message
  --version         Show version information
```

## Examples

### Basic Configuration
```bash
# Launch configurator
heros-configurator

# Install Ollama and configure local AI
# 1. Go to AI Configuration tab
# 2. Select "Ollama (Local)"
# 3. Click "Install Ollama"
# 4. Set model to "llama3.2:8b"
# 5. Click "Apply Configuration"
```

### Cloud AI Setup
```bash
# Configure Claude API
# 1. Go to AI Configuration tab
# 2. Select "Claude API (Cloud)"
# 3. Go to API Keys tab
# 4. Enter your Claude API key
# 5. Set model to "claude-3-sonnet-20240229"
# 6. Click "Apply Configuration"
```

### Hybrid Setup
```bash
# Configure hybrid mode
# 1. Go to AI Configuration tab
# 2. Select "Hybrid (Local + Cloud)"
# 3. Install Ollama for local processing
# 4. Configure cloud API keys
# 5. Set cost limits and optimization
# 6. Click "Apply Configuration"
```

## Troubleshooting

### Common Issues

#### **GTK4 Not Found**
```bash
# Install GTK4 development libraries
sudo apt install libgtk-4-dev libglib2.0-dev pkg-config
```

#### **Ollama Installation Fails**
```bash
# Manual installation
curl -fsSL https://ollama.ai/install.sh | sh
systemctl --user enable ollama
systemctl --user start ollama
```

#### **Permission Denied**
```bash
# Fix permissions
sudo chown -R $USER:$USER /etc/heros
sudo chmod 644 /etc/heros/*.conf
sudo chmod 600 /etc/heros/api/keys.conf
```

#### **Configuration Not Applied**
```bash
# Restart HER OS services
sudo systemctl restart heros-*

# Check service status
sudo systemctl status heros-*
```

### Debug Mode
```bash
# Run with debug output
G_MESSAGES_DEBUG=all heros-configurator

# Check logs
journalctl -u heros-* -f
```

## Development

### Building from Source
```bash
# Clone repository
git clone https://github.com/heros-os/heros.git
cd heros/desktop/heros_configurator

# Install dependencies
sudo apt install build-essential libgtk-4-dev libglib2.0-dev pkg-config

# Build
make build

# Run
make run

# Install
sudo make install
```

### Development Commands
```bash
# Debug build
make debug

# Release build
make release

# Clean build files
make clean

# Create package
make package

# Check dependencies
make check-deps
```

### Contributing
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## Configuration Files

### Generated Files
- `/etc/heros/heros.conf` - Main HER OS configuration
- `/etc/heros/ai.conf` - AI provider configuration
- `/etc/heros/api/keys.conf` - API keys (encrypted)
- `/etc/heros/security.conf` - Security settings
- `/etc/heros/network.conf` - Network configuration

### Backup Files
- `/var/backups/heros/config/` - Configuration backups
- `/var/backups/heros/config/YYYY-MM-DD/` - Daily backups

## Security

### API Key Security
- API keys are stored encrypted at rest
- Keys are only accessible to HER OS services
- Automatic key rotation support
- Audit logging for key access

### Configuration Security
- Configuration files have restricted permissions
- Changes are logged and audited
- Backup and restore functionality
- Validation of all configuration changes

## Support

### Documentation
- [HER OS Documentation](https://docs.heros-os.org)
- [Configuration Guide](CONFIGURATION_GUIDE.md)
- [AI Integration Guide](AI_INTEGRATION_GUIDE.md)

### Community
- [GitHub Repository](https://github.com/heros-os/heros)
- [Discussions](https://github.com/heros-os/heros/discussions)
- [Issues](https://github.com/heros-os/heros/issues)

### Support Channels
- [Email Support](mailto:support@heros-os.org)
- [Community Forum](https://forum.heros-os.org)
- [Discord Server](https://discord.gg/heros-os)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

**HER OS Configurator - Making Intelligent Computing Accessible** 🚀 