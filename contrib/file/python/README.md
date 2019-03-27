# `file-magic`: Python Bindings

This library is a Python ctypes interface to `libmagic`.


## Installing

You can install `file-magic` either with:

    python setup.py install
    # or
    easy_install .
    # or
    pip install file-magic


## Using

    import magic

    detected = magic.detect_from_filename('magic.py')
    print 'Detected MIME type: {}'.format(detected.mime_type)
    print 'Detected encoding: {}'.format(detected.encoding)
    print 'Detected file type name: {}'.format(detected.name)


## Developing/Contributing

To run the tests:

    python setup.py test
