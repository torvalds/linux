local ucl = require("ucl")

function test_simple()
  local expect =
    '['..
    '"float",1.5,'..
    '"integer",5,'..
    '"true",true,'..
    '"false",false,'..
    '"null",null,'..
    '"string","hello",'..
    '"array",[1,2],'..
    '"object",{"key":"value"}'..
    ']'

  -- Input to to_value matches the output of to_string:
  local parser = ucl.parser()
  local res,err = parser:parse_string(expect)
  if not res then
    print('parser error: ' .. err)
    return 1
  end
  
  local obj = parser:get_object()
  local got = ucl.to_json(obj, true)
  if expect == got then
    return 0
  else
   print(expect .. " == " .. tostring(got))
   return 1
  end
end

test_simple()

local table = {
  str = 'value',
  num = 100500,
  null = ucl.null,
  func = function ()
    return 'huh'
  end,
  badfunc = function()
    print("I'm bad")
  end
}

print(ucl.to_format(table, 'ucl'))
