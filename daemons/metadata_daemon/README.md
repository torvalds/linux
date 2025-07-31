# Metadata Daemon

Maintains the semantic metadata and knowledge graph for HER OS.

## Responsibilities
- Store file metadata, tags, and vector embeddings in SQLite (with vector search extension).
- Ingest events from eBPF/fanotify and update metadata.
- Provide API for querying, updating, and managing metadata.
- Integrate with WAL/2PC for transactional consistency.
- Support advanced NLQ/LLM queries and semantic enrichment.
- **Now fully implemented in Rust for performance and maintainability.**

## Core Schema (see docs/ARCHITECTURE.md)
- files
- metadata_tags
- embeddings
- events

## Implementation
- `src/main.rs`: Rust binary that initializes the SQLite database and starts an async Unix socket server for local queries.
- The schema is created automatically on first run.
- The socket server supports a line-based protocol for all commands.
- WAL/2PC polling and commit logic is fully supported.
- LLM/NLQ integration is available via Ollama or OpenAI (set OLLAMA_API_URL or OPENAI_API_KEY).

## Socket Protocol
Send a single line (UTF-8) per request. Supported commands:
- `EVENT <type> <path> [extra]` — Log a file event (CREATE, WRITE, DELETE, RENAME, etc.)
- `TAG <path> <key> <value>` — Add a tag to a file
- `EMBED <path> <embedding_type> <base64_embedding>` — Add a vector embedding
- `QUERY TAG key=value` — Query files by tag
- `NLQ <natural language query>` — Run a natural language query (uses LLM if configured)
- `HELP` — List supported commands

Responses are single-line, UTF-8 encoded.

## WAL/2PC Integration
- The daemon polls the WAL/2PC service for new file operation intents.
- For each uncommitted WAL entry, the daemon applies the operation to the metadata DB and commits it via the WAL/2PC API.
- Ensures transactional consistency and crash recovery.
- Distributed/clustered support is planned for future versions.

## NLQ/LLM Usage Examples
- `NLQ Find the files from the project I worked with John last quarter`
- `NLQ List all files tagged project:Alpha and collaborator:John modified last year`
- `NLQ Show all files in @work tagged confidential or reviewed`
- `NLQ Delete all files tagged obsolete before 2022-01-01`

The daemon will attempt to translate NLQ requests into structured queries using an LLM (Ollama, OpenAI, etc.) if configured, or fall back to built-in parsing.

## Application Manifest & Dependency Graph

### Database Schema
- application_manifests: app_name, exec_path, manifest_json, last_updated
- dependency_edges: app_id, soname, version, required, resolved_path

### Protocol Commands
- MANIFEST_SET <app> <json>: Set or update the manifest for an app. Also updates dependency_edges.
- MANIFEST_GET <app>: Get the manifest JSON for an app.
- MANIFEST_DEP_GRAPH <app>: Get the dependency graph for an app (all sonames, versions, resolved paths).

### Manifest Format
```
{
  "dependencies": [
    { "soname": "libfoo.so.1", "version": "1.2.3", "required": true, "resolved_path": "/heros_storage/libs/libfoo/1.2.3/libfoo.so.1" },
    { "soname": "libbar.so.2", "version": "2.0.0", "required": false, "resolved_path": "/heros_storage/libs/libbar/2.0.0/libbar.so.2" }
  ]
}
```

### Usage Examples
- Set manifest:
  `echo 'MANIFEST_SET myapp {"dependencies":[{"soname":"libfoo.so.1","version":"1.2.3","required":true,"resolved_path":"/heros_storage/libs/libfoo/1.2.3/libfoo.so.1"}]}' | socat - UNIX-CONNECT:/tmp/heros_metadata.sock`
- Get manifest:
  `echo 'MANIFEST_GET myapp' | socat - UNIX-CONNECT:/tmp/heros_metadata.sock`
- Get dependency graph:
  `echo 'MANIFEST_DEP_GRAPH myapp' | socat - UNIX-CONNECT:/tmp/heros_metadata.sock`

### Integration
- The heros-linker-shim.so queries MANIFEST_DEP_GRAPH for the current process and redirects .so opens to the deduplicated, versioned path.
- All actions are logged for audit and debugging.

## Running and Testing
1. Build and start the daemon:
   ```sh
   cargo build --release
   ./target/release/metadata_daemon
   ```
2. Use a Unix socket client to send commands (see protocol above).
   Example:
   ```sh
   echo 'TAG /tmp/foo.txt project Alpha' | socat - UNIX-CONNECT:/tmp/heros_metadata.sock
   echo 'QUERY TAG project=Alpha' | socat - UNIX-CONNECT:/tmp/heros_metadata.sock
   ```

## Migration Note
- The Python version is now deprecated. All new development and testing should use the Rust implementation.
- See `src/main.rs` for full details and extension points. 