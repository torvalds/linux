# Version history

## Libucl 0.5

- Streamline emitter has been added, so it is now possible to output partial `ucl` objects
- Emitter now is more flexible due to emitter_context structure

### 0.5.1
- Fixed number of bugs and memory leaks

### 0.5.2

- Allow userdata objects to be emitted and destructed
- Use userdata objects to store lua function references

### Libucl 0.6

- Reworked macro interface

### Libucl 0.6.1

- Various utilities fixes

### Libucl 0.7.0

- Move to klib library from uthash to reduce memory overhead and increase performance

### Libucl 0.7.1

- Added safe iterators API

### Libucl 0.7.2

- Fixed serious bugs in schema and arrays iteration

### Libucl 0.7.3

- Fixed a bug with macros that come after an empty object
- Fixed a bug in include processing when an incorrect variable has been destroyed (use-after-free)

### Libucl 0.8.0

- Allow to save comments and macros when parsing UCL documents
- C++ API
- Python bindings (by Eitan Adler)
- Add msgpack support for parser and emitter
- Add Canonical S-expressions parser for libucl
- CLI interface for parsing and validation (by Maxim Ignatenko)
- Implement include with priority
- Add 'nested' functionality to .include macro (by Allan Jude)
- Allow searching an array of paths for includes (by Allan Jude)
- Add new .load macro (by Allan Jude)
- Implement .inherit macro (#100)
- Add merge strategies
- Add schema validation to lua API
- Add support for external references to schema validation
- Add coveralls integration to libucl
- Implement tests for 80% of libucl code lines
- Fix tonns of minor and major bugs
- Improve documentation
- Rework function names to the common conventions (old names are preserved for backwards compatibility)
- Add Coverity scan integration
- Add fuzz tests

**Incompatible changes**:

- `ucl_object_emit_full` now accepts additional argument `comments` that could be used to emit comments with UCL output