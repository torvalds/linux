.. SPDX-License-Identifier: GPL-2.0

Writing Devicetree Bindings in json-schema
==========================================

Devicetree bindings are written using json-schema vocabulary. Schema files are
written in a JSON-compatible subset of YAML. YAML is used instead of JSON as it
is considered more human readable and has some advantages such as allowing
comments (Prefixed with '#').

Also see :ref:`example-schema`.

Schema Contents
---------------

Each schema doc is a structured json-schema which is defined by a set of
top-level properties. Generally, there is one binding defined per file. The
top-level json-schema properties used are:

$id
  A json-schema unique identifier string. The string must be a valid
  URI typically containing the binding's filename and path. For DT schema, it must
  begin with "http://devicetree.org/schemas/". The URL is used in constructing
  references to other files specified in schema "$ref" properties. A $ref value
  with a leading '/' will have the hostname prepended. A $ref value with only a
  relative path or filename will be prepended with the hostname and path
  components of the current schema file's '$id' value. A URL is used even for
  local files, but there may not actually be files present at those locations.

$schema
  Indicates the meta-schema the schema file adheres to.

title
  A one-line description of the hardware being described in the binding schema.

maintainers
  A DT specific property. Contains a list of email address(es)
  for maintainers of this binding.

description
  Optional. A multi-line text block containing any detailed
  information about this hardware. It should contain things such as what the block
  or device does, standards the device conforms to, and links to datasheets for
  more information.

  The YAML format has several options for defining the formatting of the text
  block. The options are controlled with indicator characters following the key
  (e.g. "description: \|"). The minimum formatting needed for a block should be
  used. The formatting controls can not only affect whether the YAML can be
  parsed correctly, but are important when the text blocks are rendered to
  another form. The options are as follows.

  The default without any indicators is flowed, plain scalar style where single
  line breaks and leading whitespace are stripped. Paragraphs are delimited by
  blank lines (i.e. double line break). This style cannot contain ": " in it as
  it will be interpreted as a key. Any " #" sequence will be interpreted as
  a comment. There's other restrictions on characters as well. Most
  restrictions are on what the first character can be.

  The second style is folded which is indicated by ">" character. In addition
  to maintaining line breaks on double line breaks, the folded style also
  maintains leading whitespace beyond indentation of the first line. The line
  breaks on indented lines are also maintained.

  The third style is literal which is indicated by "\|" character. The literal
  style maintains all line breaks and whitespace (beyond indentation of the
  first line).

  The above is not a complete description of YAML text blocks. More details on
  multi-line YAML text blocks can be found online:

  https://yaml-multiline.info/

  https://www.yaml.info/learn/quote.html

select
  Optional. A json-schema used to match nodes for applying the
  schema. By default, without 'select', nodes are matched against their possible
  compatible-string values or node name. Most bindings should not need select.

allOf
  Optional. A list of other schemas to include. This is used to
  include other schemas the binding conforms to. This may be schemas for a
  particular class of devices such as I2C or SPI controllers.

properties
  A set of sub-schema defining all the DT properties for the
  binding. The exact schema syntax depends on whether properties are known,
  common properties (e.g. 'interrupts') or are binding/vendor-specific
  properties.

A property can also define a child DT node with child properties defined
under it.

For more details on properties sections, see 'Property Schema' section.

patternProperties
  Optional. Similar to 'properties', but names are regex.

required
  A list of DT properties from the 'properties' section that
  must always be present.

additionalProperties / unevaluatedProperties
  Keywords controlling how schema will validate properties not matched by this
  schema's 'properties' or 'patternProperties'. Each schema is supposed to
  have exactly one of these keywords in top-level part, so either
  additionalProperties or unevaluatedProperties. Nested nodes, so properties
  being objects, are supposed to have one as well.

  * additionalProperties: false
      Most common case, where no additional schema is referenced or if this
      binding allows subset of properties from other referenced schemas.

  * unevaluatedProperties: false
      Used when this binding references other schema whose all properties
      should be allowed.

  * additionalProperties: true
      - Top-level part:
        Rare case, used for schemas implementing common set of properties. Such
        schemas are supposed to be referenced by other schemas, which then use
        'unevaluatedProperties: false'.  Typically bus or common-part schemas.
      - Nested node:
        When listing only the expected compatible of the nested node and there
        is an another schema matching that compatible which ends with one of
        two above cases ('false').

examples
  Optional. A list of one or more DTS hunks implementing this binding only.
  Example should not contain unrelated device nodes, e.g. consumer nodes in a
  provider binding, other nodes referenced by phandle.
  Note: YAML doesn't allow leading tabs, so spaces must be used instead.

Unless noted otherwise, all properties are required.

Property Schema
---------------

The 'properties' section of the schema contains all the DT properties for a
binding. Each property contains a set of constraints using json-schema
vocabulary for that property. The properties schemas are what are used for
validation of DT files.

For common properties, only additional constraints not covered by the common,
binding schema need to be defined such as how many values are valid or what
possible values are valid.

Vendor-specific properties will typically need more detailed schema. With the
exception of boolean properties, they should have a reference to a type in
schemas/types.yaml. A "description" property is always required.

The Devicetree schemas don't exactly match the YAML-encoded DT data produced by
dtc. They are simplified to make them more compact and avoid a bunch of
boilerplate. The tools process the schema files to produce the final schema for
validation. There are currently 2 transformations the tools perform.

The default for arrays in json-schema is they are variable-sized and allow more
entries than explicitly defined. This can be restricted by defining 'minItems',
'maxItems', and 'additionalItems'. However, for DeviceTree Schemas, a fixed
size is desired in most cases, so these properties are added based on the
number of entries in an 'items' list.

The YAML Devicetree format also makes all string values an array and scalar
values a matrix (in order to define groupings) even when only a single value
is present. Single entries in schemas are fixed up to match this encoding.

When bindings cover multiple similar devices that differ in some properties,
those properties should be constrained for each device. This usually means:

 * In top level 'properties' define the property with the broadest constraints.
 * In 'if:then:' blocks, further narrow the constraints for those properties.
 * Do not define the properties within an 'if:then:' block (note that
   'additionalItems' also won't allow that).

Coding style
------------

Use YAML coding style (two-space indentation). For DTS examples in the schema,
preferred is four-space indentation.

Place entries in 'properties' and 'required' sections in the same order, using
style from Documentation/devicetree/bindings/dts-coding-style.rst.

Testing
-------

Dependencies
~~~~~~~~~~~~

The DT schema project must be installed in order to validate the DT schema
binding documents and validate DTS files using the DT schema. The DT schema
project can be installed with pip::

    pip3 install dtschema

Note that 'dtschema' installation requires 'swig' and Python development files
installed first. On Debian/Ubuntu systems::

    apt install swig python3-dev

Several executables (dt-doc-validate, dt-mk-schema, dt-validate) will be
installed. Ensure they are in your PATH (~/.local/bin by default).

Recommended is also to install yamllint (used by dtschema when present).

Running checks
~~~~~~~~~~~~~~

The DT schema binding documents must be validated using the meta-schema (the
schema for the schema) to ensure they are both valid json-schema and valid
binding schema. All of the DT binding documents can be validated using the
``dt_binding_check`` target::

    make dt_binding_check

In order to perform validation of DT source files, use the ``dtbs_check`` target::

    make dtbs_check

Note that ``dtbs_check`` will skip any binding schema files with errors. It is
necessary to use ``dt_binding_check`` to get all the validation errors in the
binding schema files.

It is possible to run both in a single command::

    make dt_binding_check dtbs_check

It is also possible to run checks with a subset of matching schema files by
setting the ``DT_SCHEMA_FILES`` variable to 1 or more specific schema files or
patterns (partial match of a fixed string). Each file or pattern should be
separated by ':'.

::

    make dt_binding_check DT_SCHEMA_FILES=trivial-devices.yaml
    make dt_binding_check DT_SCHEMA_FILES=trivial-devices.yaml:rtc.yaml
    make dt_binding_check DT_SCHEMA_FILES=/gpio/
    make dtbs_check DT_SCHEMA_FILES=trivial-devices.yaml


json-schema Resources
---------------------


`JSON-Schema Specifications <http://json-schema.org/>`_

`Using JSON Schema Book <http://usingjsonschema.com/>`_

.. _example-schema:

Annotated Example Schema
------------------------

Also available as a separate file: :download:`example-schema.yaml`

.. literalinclude:: example-schema.yaml
