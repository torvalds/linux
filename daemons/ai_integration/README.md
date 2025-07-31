# AI Integration Module

Orchestrates local and cloud-based AI for file analysis and semantic enrichment.

## Implementation (C)
- `ai_integration.c`: Main daemon, written in C for performance and Linux compatibility.
- Listens on a Unix domain socket (`/tmp/heros_ai_integration.sock`).
- Handles incoming requests with a simple protocol for submitting file analysis tasks.
- Maintains a task queue and worker thread for processing tasks.
- Stubs for future AI task processing (ONNX Runtime, LLM APIs, etc.).

## Protocol
Send a single line (UTF-8) per request. Supported commands:
- `ANALYZE <file_path> <task_type>` (task_type: NER | TOPIC | EMBED | ALL)
- `HELP`

Responses:
- `OK: Task queued`
- `ERR: <reason>`

## Usage
Compile:
```sh
gcc -o ai_integration ai_integration.c -lpthread
```
Run:
```sh
./ai_integration
``` 