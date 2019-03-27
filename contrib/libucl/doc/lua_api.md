## Module `ucl`

This lua module allows to parse objects from strings and to store data into
ucl objects. It uses `libucl` C library to parse and manipulate with ucl objects.

Example:

~~~lua
local ucl = require("ucl")

local parser = ucl.parser()
local res,err = parser:parse_string('{key=value}')

if not res then
	print('parser error: ' .. err)
else
	local obj = parser:get_object()
	local got = ucl.to_format(obj, 'json')
end

local table = {
  str = 'value',
  num = 100500,
  null = ucl.null,
  func = function ()
    return 'huh'
  end
}


print(ucl.to_format(table, 'ucl'))
-- Output:
--[[
num = 100500;
str = "value";
null = null;
func = "huh";
--]]
~~~

###Brief content:

**Functions**:

> [`ucl_object_push_lua(L, obj, allow_array)`](#function-ucl_object_push_lual-obj-allow_array)

> [`ucl.to_format(var, format)`](#function-uclto_formatvar-format)



**Methods**:

> [`parser:parse_file(name)`](#method-parserparse_filename)

> [`parser:parse_string(input)`](#method-parserparse_stringinput)

> [`parser:get_object()`](#method-parserget_object)


## Functions

The module `ucl` defines the following functions.

### Function `ucl_object_push_lua(L, obj, allow_array)`

This is a `C` function to push `UCL` object as lua variable. This function
converts `obj` to lua representation using the following conversions:

- *scalar* values are directly presented by lua objects
- *userdata* values are converted to lua function objects using `LUA_REGISTRYINDEX`,
this can be used to pass functions from lua to c and vice-versa
- *arrays* are converted to lua tables with numeric indicies suitable for `ipairs` iterations
- *objects* are converted to lua tables with string indicies

**Parameters:**

- `L {lua_State}`: lua state pointer
- `obj {ucl_object_t}`: object to push
- `allow_array {bool}`: expand implicit arrays (should be true for all but partial arrays)

**Returns:**

- `{int}`: `1` if an object is pushed to lua

Back to [module description](#module-ucl).

### Function `ucl.to_format(var, format)`

Converts lua variable `var` to the specified `format`. Formats supported are:

- `json` - fine printed json
- `json-compact` - compacted json
- `config` - fine printed configuration
- `ucl` - same as `config`
- `yaml` - embedded yaml

If `var` contains function, they are called during output formatting and if
they return string value, then this value is used for ouptut.

**Parameters:**

- `var {variant}`: any sort of lua variable (if userdata then metafield `__to_ucl` is searched for output)
- `format {string}`: any available format

**Returns:**

- `{string}`: string representation of `var` in the specific `format`.

Example:

~~~lua
local table = {
  str = 'value',
  num = 100500,
  null = ucl.null,
  func = function ()
    return 'huh'
  end
}


print(ucl.to_format(table, 'ucl'))
-- Output:
--[[
num = 100500;
str = "value";
null = null;
func = "huh";
--]]
~~~

Back to [module description](#module-ucl).


## Methods

The module `ucl` defines the following methods.

### Method `parser:parse_file(name)`

Parse UCL object from file.

**Parameters:**

- `name {string}`: filename to parse

**Returns:**

- `{bool[, string]}`: if res is `true` then file has been parsed successfully, otherwise an error string is also returned

Example:

~~~lua
local parser = ucl.parser()
local res,err = parser:parse_file('/some/file.conf')

if not res then
	print('parser error: ' .. err)
else
	-- Do something with object
end
~~~

Back to [module description](#module-ucl).

### Method `parser:parse_string(input)`

Parse UCL object from file.

**Parameters:**

- `input {string}`: string to parse

**Returns:**

- `{bool[, string]}`: if res is `true` then file has been parsed successfully, otherwise an error string is also returned

Back to [module description](#module-ucl).

### Method `parser:get_object()`

Get top object from parser and export it to lua representation.

**Parameters:**

	nothing

**Returns:**

- `{variant or nil}`: ucl object as lua native variable

Back to [module description](#module-ucl).


Back to [top](#).

