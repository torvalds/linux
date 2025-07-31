# HER OS User-Space Daemons

This directory will contain the core user-space daemons for HER OS:

- **Metadata Daemon**: Maintains the semantic metadata database (SQLite + vector search).
- **AI Integration Module**: Orchestrates local and cloud-based AI for file analysis and embedding.
- **Data Tiering Daemon**: Manages hot/cold data migration using BTRFS send/receive.
- **Policy Decision Point (PDP)**: Handles access control decisions for the custom LSM.
- **action_daemon/**: AT-SPI Action Layer daemon for semantic UI automation and context ingestion (C, kernel-style)
- **pta_engine/**: Proactive Task Anticipation Engine for intelligent automation and context-aware suggestions (C, kernel-style)

Each daemon will have its own subdirectory, documentation, and API/interface description.

See `docs/ARCHITECTURE.md` and `Project/Project Details` for design details. 