# Python `file-magic` Log of Changes

## `0.4.0`

- Sync with current version of file:
  * Retain python 2 compatibility, factoring out the conversion functions.
  * Avoid double encoding with python3
  * Restore python-2 compatibility.


## `0.3.0`

- Fix `setup.py` so we can upload to PyPI
- Add function `detect_from_filename`
- Add function `detect_from_fobj`
- Add function `detect_from_content`
