.. SPDX-License-Identifier: GPL-2.0

.. _pt_BR_codingstyle:

Estilo de codificação do kernel Linux
=====================================

Este é um breve documento descrevendo o estilo de codificação preferido
para o kernel Linux. O estilo de codificação é muito pessoal, e eu não
**forçarei** minhas opiniões a ninguém, mas isso é o que vale para tudo
que eu tenha que manter, e eu preferiria isso para a maioria das outras
coisas também. Por favor, considere pelo menos os pontos aqui apresentados.

Primeiramente, eu sugiro imprimir uma cópia dos padrões de codificação do GNU,
e NÃO lê-la. Queime-as, é um grande gesto simbólico.

De qualquer forma, aqui vai:


1) Indentação
-------------

As tabulações têm 8 caracteres, e portanto as indentações também têm 8
caracteres. Há movimentos heréticos que tentam fazer com que as indentações
tenham 4 (ou até 2!) caracteres de profundidade, e isso é semelhante a
tentar definir o valor de PI como 3.

Justificativa: A ideia central da indentação é definir claramente onde um
bloco de controle começa e termina. Especialmente quando você esteve olhando
para a tela por 20 horas seguidas, você vai achar muito mais fácil ver como
a indentação funciona se ela for mais ampla.

Agora, algumas pessoas afirmarão que ter indentações de 8 caracteres faz
o código se deslocar demais para a direita e dificultam a leitura em um
terminal de 80 caracteres. A resposta é que, se você precisar de mais de 3
níveis de indentação, você já está em apuros de qualquer forma, e deve
corrigir seu programa.

Em resumo, indentações de 8 caracteres deixam as coisas mais fáceis de ler, e
têm o benefício adicional de avisar quando você está aninhando suas funções
em excesso. Atenda a esse aviso.

A maneira preferida de aliviar vários níveis de indentação em uma instrução
``switch`` é alinhar o ``switch`` e seus rótulos subordinados ``case`` na
mesma coluna, em vez de indentar duplamente os rótulos ``case``. Por exemplo:

.. code-block:: c

	switch (suffix) {
	case 'G':
	case 'g':
		mem <<= 30;
		break;
	case 'M':
	case 'm':
		mem <<= 20;
		break;
	case 'K':
	case 'k':
		mem <<= 10;
		fallthrough;
	default:
		break;
	}

Não coloque múltiplas instruções em uma única linha, a menos que você tenha
algo para esconder:

.. code-block:: c

	if (condition) do_this;
	  do_something_everytime;

Não use vírgulas para evitar usar chaves:

.. code-block:: c

	if (condition)
		do_this(), do_that();

Sempre use chaves para múltiplas instruções:

.. code-block:: c

	if (condition) {
		do_this();
		do_that();
	}

Também não coloque múltiplas atribuições em uma única linha. O estilo de
codificação do kernel é extremamente simples. Evite expressões complicadas.


Fora de comentários, documentação e, exceto em Kconfig, espaços nunca são
usados para indentação, e o exemplo acima foi deliberadamente quebrado.

Obtenha um editor decente e não deixe espaços em branco no final das linhas.


2) Quebrando linhas longas e strings
------------------------------------

O estilo de codificação trata principalmente da legibilidade e da
manutenibilidade usando ferramentas comumente disponíveis.

O limite preferido para o comprimento de uma única linha é de 80 colunas.

Instruções com mais de 80 colunas devem ser quebradas em partes sensatas,
a menos que exceder 80 colunas aumente significativamente a legibilidade e
não esconda informações.

Os descendentes são sempre substancialmente mais curtos do que o pai e são
colocados substancialmente à direita. Um estilo muito usado é alinhar os
descendentes ao parêntese de abertura de uma função.

Essas mesmas regras são aplicadas aos cabeçalhos de função com uma lista de
argumentos longa.

No entanto, nunca quebre strings visíveis ao usuário, como mensagens
``printk``, porque isso prejudica a capacidade de fazer ``grep`` nelas.


3) Posicionamento de chaves e espaços
-------------------------------------

A outra questão que sempre surge no estilo em C é o posicionamento das
chaves. Ao contrário do tamanho da indentação, há poucos motivos técnicos
para escolher uma estratégia de posicionamento em vez de outra, mas a forma
preferida, como nos mostraram os profetas Kernighan e Ritchie, é colocar a
chave de abertura no final da linha e a chave de fechamento no início, assim:

.. code-block:: c

	if (x is true) {
		we do y
	}

Isso se aplica a todos os blocos de instruções que não sejam funções (if,
switch, for, while, do). Por exemplo:

.. code-block:: c

	switch (action) {
	case KOBJ_ADD:
		return "add";
	case KOBJ_REMOVE:
		return "remove";
	case KOBJ_CHANGE:
		return "change";
	default:
		return NULL;
	}

No entanto, há um caso especial: as funções, que têm a chave de abertura no
início da linha seguinte, assim:

.. code-block:: c

	int function(int x)
	{
		body of function
	}

Pessoas heréticas por todo o mundo afirmaram que essa inconsistência é ...
bem ... inconsistente, mas todas as pessoas de bom senso sabem que (a) K&R
estão **corretos** e (b) K&R estão certos. Além disso, as funções são
especiais de qualquer forma (você não pode aninhá-las em C).

Observe que a chave de fechamento fica vazia em uma linha própria, **exceto**
nos casos em que ela é seguida por uma continuação da mesma instrução, ou
seja, um ``while`` em um do-statement ou um ``else`` em um if-statement, como
neste exemplo:

.. code-block:: c

	do {
		body of do-loop
	} while (condition);

e

.. code-block:: c

	if (x == y) {
		..
	} else if (x > y) {
		...
	} else {
		....
	}

Justificativa: K&R.

Além disso, observe que esse posicionamento de chaves também minimiza o
número de linhas vazias (ou quase vazias), sem qualquer perda de
legibilidade. Assim, como o suprimento de linhas novas na sua tela não é um
recurso renovável (pense em telas de terminal de 25 linhas), você tem mais
linhas vazias para colocar comentários.

Não use chaves desnecessariamente quando uma única instrução basta.

.. code-block:: c

	if (condition)
		action();

e

.. code-block:: c

	if (condition)
		do_this();
	else
		do_that();

Isso não se aplica se apenas um ramo de uma instrução condicional for uma
única instrução; nesse último caso, use chaves em ambos os ramos:

.. code-block:: c

	if (condition) {
		do_this();
		do_that();
	} else {
		otherwise();
	}

Além disso, use chaves quando um laço contiver mais de uma instrução simples:

.. code-block:: c

	while (condition) {
		if (test)
			do_something();
	}

3.1) Espaços
************

O estilo do kernel Linux para o uso de espaços depende (em grande parte) do
uso de função versus palavra-chave. Use um espaço após (a maioria das)
palavras-chave. As exceções notáveis são ``sizeof``, ``typeof``, ``alignof`` e
``__attribute__``, que parecem um pouco com funções (e geralmente são usadas
com parênteses no Linux, embora não sejam obrigatórias na linguagem, como em:
``sizeof info`` depois que ``struct fileinfo info;`` é declarado).

Então use um espaço após estas palavras-chave::

	if, switch, case, for, do, while

mas não com ``sizeof``, ``typeof``, ``alignof`` ou ``__attribute__``. Por
exemplo,

.. code-block:: c


	s = sizeof(struct file);

Não adicione espaços ao redor (dentro) de expressões entre parênteses. Este
exemplo é **ruim**:

.. code-block:: c


	s = sizeof( struct file );

Ao declarar dados de ponteiro ou uma função que retorna um tipo de ponteiro, o
uso preferido de ``*`` fica adjacente ao nome dos dados ou ao nome da função e
não adjacente ao nome do tipo. Exemplos:

.. code-block:: c


	char *linux_banner;
	unsigned long long memparse(char *ptr, char **retptr);
	char *match_strdup(substring_t *s);

Use um espaço em volta (de cada lado) da maioria dos operadores binários e
ternários, como qualquer um destes::

	=  +  -  <  >  *  /  %  |  &  ^  <=  >=  ==  !=  ?  :

mas sem espaço após operadores unários::

	&  *  +  -  ~  !  sizeof  typeof  alignof  __attribute__  defined

sem espaço antes dos operadores unários pós-fixados de incremento e
decremento::

	++  --

sem espaço após os operadores unários prefixados de incremento e
decremento::

	++  --

e sem espaço ao redor dos operadores de membro de estrutura ``.`` e ``->``.

Não deixe espaços em branco no final das linhas. Alguns editores com
indentação ``inteligente`` inserem espaços no início das novas linhas conforme
apropriado, para que você possa começar a digitar a próxima linha de código
imediatamente. No entanto, alguns desses editores não removem o espaço em
branco se você acabar não colocando uma linha de código ali, como quando deixa
uma linha em branco. Como resultado, você termina com linhas contendo espaço
em branco no final.

O Git avisará você sobre patches que introduzem espaço em branco no final e
pode remover esse espaço automaticamente para você; entretanto, se você
aplicar uma série de patches, isso pode fazer com que patches posteriores da
série falhem ao alterar suas linhas de contexto.


4) Nomeação
-----------

C é uma linguagem espartana, e suas convenções de nomenclatura devem seguir o
mesmo caminho. Ao contrário dos programadores em Modula-2 e Pascal, os
programadores em C não usam nomes bonitinhos como ThisVariableIsATemporaryCounter.
Um programador em C chamaria essa variável de ``tmp``, o que é muito mais fácil
de escrever e não é menos fácil de entender.

ENTRETANTO, embora nomes em camelCase sejam desencorajados, nomes descritivos
para variáveis globais são essenciais. Chamar uma função global de ``foo`` é
um crime.

Variáveis GLOBAIS (a serem usadas somente se você **realmente** precisar) devem
ter nomes descritivos, assim como as funções globais. Se você tiver uma função
que conta o número de usuários ativos, você deve chamá-la de
``count_active_users()`` ou algo parecido; você **não** deve chamá-la de
``cntusr()``.

Codificar o tipo de uma função no nome (a chamada notação Húngara) é absurdo -
o compilador conhece os tipos de qualquer forma e pode verificar isso, e isso
só confunde o programador.

Nomes de variáveis locais devem ser curtos e diretos. Se você tiver algum
contador inteiro aleatório de loop, provavelmente deve ser chamado de ``i``.
Chamá-lo de ``loop_counter`` é improdutivo, se não houver chance de ser mal
interpretado. Da mesma forma, ``tmp`` pode ser praticamente qualquer tipo de
variável usada para manter um valor temporário.

Se você tem medo de misturar os nomes de variáveis locais, você tem outro
problema, chamado síndrome de desequilíbrio de hormônio de crescimento da
função. Veja o capítulo 6 (Funções).

Para nomes de símbolos e documentação, evite introduzir o uso novo de
'master / slave' (ou 'slave' independente de 'master') e 'blacklist /
whitelist'.

Substituições recomendadas para 'master / slave' são:
    '{primary,main} / {secondary,replica,subordinate}'
    '{initiator,requester} / {target,responder}'
    '{controller,host} / {device,worker,proxy}'
    'leader / follower'
    'director / performer'

Substituições recomendadas para 'blacklist/whitelist' são:
    'denylist / allowlist'
    'blocklist / passlist'

Exceções para introduzir novos usos são manter uma ABI/API do espaço do
usuário, ou ao atualizar código para um hardware ou especificação de
protocolo existente (a partir de 2020) que exija esses termos. Para novas
especificações, traduza o uso da terminologia na especificação para o padrão
de codificação do kernel quando possível.

5) Tipos definidos (typedefs)
-----------------------------

Por favor, não use coisas como ``vps_t``.
É um **erro** usar ``typedef`` para estruturas e ponteiros. Quando você vê

.. code-block:: c


	vps_t a;

no código-fonte, o que isso significa?
Em contraste, se diz

.. code-block:: c

	struct virtual_container *a;

você consegue dizer o que ``a`` é.

Muitas pessoas pensam que ``typedef``s ``ajudam na legibilidade``. Não é bem
assim. Eles são úteis apenas para:

 (a) objetos totalmente opacos (onde o ``typedef`` é usado ativamente para
     **ocultar** o que o objeto é).

     Exemplo: ``pte_t`` etc. Objetos opacos que você só pode acessar usando
     as funções de acesso apropriadas.

     .. note::

       Opacidade e ``funções de acesso`` não são boas em si mesmas.
       A razão pela qual as temos para coisas como ``pte_t`` etc. é que
       realmente existe absolutamente **zero** informação acessível de forma portátil
       ali.

 (b) tipos inteiros claros, em que a abstração **ajuda** a evitar confusão
     sobre se é ``int`` ou ``long``.

     ``u8/u16/u32`` são typedefs perfeitamente aceitáveis, embora se
     encaixem melhor na categoria (d) do que aqui.

     .. note::

       Novamente - precisa haver uma **razão** para isso. Se algo é
       ``unsigned long``, não há motivo para fazer

	typedef unsigned long myflags_t;

     mas se houver uma razão clara para que em certas circunstâncias possa
     ser ``unsigned int`` e em outras configurações possa ser ``unsigned
     long``, então, claro, use um ``typedef``.

 (c) quando você usa ``sparse`` para criar literalmente um **novo** tipo para
     verificação de tipos.

 (d) novos tipos idênticos aos tipos padrão do C99, em certas
     circunstâncias excepcionais.

     Embora levasse apenas um curto período para os olhos e o cérebro se
     acostumarem aos tipos padrão como ``uint32_t``, algumas pessoas ainda
     se opõem ao seu uso.

     Portanto, os tipos específicos do Linux ``u8/u16/u32/u64`` e seus
     equivalentes assinados, que são idênticos aos tipos padrão, são
     permitidos -- embora não sejam obrigatórios em código novo seu.

     Ao editar código existente que já usa um ou outro conjunto de tipos, você
     deve seguir as escolhas existentes nesse código.

 (e) tipos seguros para uso em espaço do usuário.

     Em certas estruturas visíveis ao espaço do usuário, não podemos exigir
     tipos C99 nem usar a forma ``u32`` acima. Portanto, usamos ``__u32`` e
     tipos semelhantes em todas as estruturas compartilhadas com o espaço do
     usuário.

Pode haver outros casos também, mas a regra básica deve ser: NUNCA use um
``typedef`` a menos que você consiga encaixar claramente em uma dessas regras.

Em geral, um ponteiro, ou uma estrutura com elementos que podem ser acessados
diretamente, **nunca** deve ser um ``typedef``.


6) Funções
----------

As funções devem ser curtas e diretas, e fazer apenas uma coisa. Elas devem
caber em uma ou duas telas de texto (o tamanho de tela ISO/ANSI é 80x24,
como todos sabem), e fazer uma coisa e fazê-la bem.

O comprimento máximo de uma função é inversamente proporcional à complexidade
e ao nível de indentação dessa função. Então, se você tiver uma função
conceitualmente simples que seja apenas uma longa (mas simples) instrução
``switch``, em que você precisa fazer várias pequenas coisas para muitos casos
diferentes, é aceitável ter uma função mais longa.

No entanto, se você tiver uma função complexa e suspeitar que um estudante
do primeiro ano do ensino médio, menos talentoso, talvez nem entenda do que
se trata a função, você deve obedecer aos limites máximos ainda mais de
perto. Use funções auxiliares com nomes descritivos (você pode pedir ao
compilador para incorporá-las em linha se achar que é crítico para o
desempenho, e provavelmente ele fará um trabalho melhor do que você faria).

Outra medida da função é o número de variáveis locais. Elas não devem exceder
5-10, ou algo está errado. Reflita sobre a função e divida em partes menores.
Um cérebro humano geralmente consegue rastrear facilmente cerca de 7 coisas
diferentes; qualquer quantidade acima disso o confunde. Você sabe que é
brilhante, mas talvez queira entender o que fez daqui a 2 semanas.

Nos arquivos de código-fonte, separe as funções com uma linha em branco. Se a
função for exportada, a macro **EXPORT** para ela deve seguir imediatamente
depois da linha da chave de fechamento da função. Por exemplo:

.. code-block:: c

	int system_is_up(void)
	{
		return system_state == SYSTEM_RUNNING;
	}
	EXPORT_SYMBOL(system_is_up);

6.1) Protótipos de função
*************************

Nos protótipos de função, inclua nomes de parâmetros junto com seus tipos de
dados. Embora isso não seja obrigatório pela linguagem C, é preferido no Linux
porque é uma maneira simples de adicionar informações valiosas para o leitor.

Não use a palavra-chave ``extern`` em declarações de função, pois isso torna
as linhas mais longas e não é estritamente necessário.

Ao escrever protótipos de função, por favor mantenha a `ordem dos elementos
regular <https://lore.kernel.org/mm-commits/CAHk-=wiOCLRny5aifWNhr621kYrJwhfURsa0vFPeUEm8mF0ufg@mail.gmail.com/>`_.
Por exemplo, usando este exemplo de declaração de função::

 __init void * __must_check action(enum magic value, size_t size, u8 count,
				   char *fmt, ...) __printf(4, 5) __malloc;

A ordem preferida dos elementos para um protótipo de função é:

- classe de armazenamento (abaixo, ``static __always_inline``, observando que
  ``__always_inline`` é tecnicamente um atributo, mas é tratado como ``inline``)
- atributos de classe de armazenamento (aqui, ``__init`` -- ou seja,
  declarações de seção, mas também coisas como ``__cold``)
- tipo de retorno (aqui, ``void *``)
- atributos do tipo de retorno (aqui, ``__must_check``)
- nome da função (aqui, ``action``)
- parâmetros da função (aqui, ``(enum magic value, size_t size, u8 count,
  char *fmt, ...)``, observando que os nomes dos parâmetros devem sempre ser
  incluídos)
- atributos dos parâmetros da função (aqui, ``__printf(4, 5)``)
- atributos de comportamento da função (aqui, ``__malloc``)

Observe que, para uma **definição** de função (ou seja, o corpo real da
função), o compilador não permite atributos de parâmetro da função depois dos
parâmetros da função. Nesses casos, eles devem vir depois dos atributos da
classe de armazenamento (por exemplo, observe a posição alterada de
``__printf(4, 5)`` abaixo, em comparação com o exemplo de **declaração**
acima)::

 static __always_inline __init __printf(4, 5) void * __must_check action(enum magic value,
		size_t size, u8 count, char *fmt, ...) __malloc
 {
	...
 }

7) Saída centralizada de funções
--------------------------------

Embora seja depreciado por algumas pessoas, o equivalente da instrução
``goto`` é usado com frequência pelos compiladores na forma da instrução de
salto incondicional.

A instrução ``goto`` é útil quando uma função sai de vários pontos e é
necessária alguma tarefa comum, como limpeza. Se não for necessária nenhuma
limpeza, basta retornar diretamente.

Escolha nomes de rótulos que indiquem o que o ``goto`` faz ou por que ele
existe. Um exemplo de nome bom poderia ser ``out_free_buffer:`` se o goto
liberar ``buffer``. Evite usar nomes do GW-BASIC como ``err1:`` e ``err2:``,
porque você teria que renumerá-los se adicionasse ou removesse caminhos de
saída, e isso também torna a correção mais difícil de verificar.

A justificativa para usar gotos é:

- instruções incondicionais são mais fáceis de entender e seguir
- o aninhamento é reduzido
- erros por não atualizar pontos de saída individuais ao fazer
  modificações são evitados
- economiza o trabalho do compilador de otimizar e remover código redundante ;)

.. code-block:: c

	int fun(int a)
	{
		int result = 0;
		char *buffer;

		buffer = kmalloc(SIZE, GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;

		if (condition1) {
			while (loop1) {
				...
			}
			result = 1;
			goto out_free_buffer;
		}
		...
	out_free_buffer:
		kfree(buffer);
		return result;
	}

Um tipo comum de bug do qual você deve estar ciente é o ``one err bugs``
(o "bug de um erro"), que se parece com isto:

.. code-block:: c

	err:
		kfree(foo->bar);
		kfree(foo);
		return ret;

O problema neste código é que, em alguns caminhos de saída, ``foo`` é NULL.
Normalmente, a correção é dividir em dois rótulos de erro
``err_free_bar:`` e ``err_free_foo:``:

.. code-block:: c

	err_free_bar:
		kfree(foo->bar);
	err_free_foo:
		kfree(foo);
		return ret;

Idealmente, você deve simular erros para testar todos os caminhos de saída.


8) Comentários
--------------

Comentários são bons, mas também há o perigo de comentar demais. NUNCA tente
explicar COMO o seu código funciona em um comentário: é muito melhor escrever
o código de forma que o **funcionamento** seja óbvio, e é um desperdício de
tempo explicar código mal escrito.

Em geral, você quer que seus comentários digam O QUE o seu código faz, não
COMO. Também tente evitar colocar comentários dentro do corpo de uma função:
se a função for tão complexa que você precisa comentar partes separadas dela,
provavelmente você deveria voltar ao capítulo 6 por um tempo. Você pode fazer
pequenos comentários para notar ou avisar sobre algo particularmente esperto
(ou feio), mas tente evitar excesso. Em vez disso, coloque os comentários no
início da função, dizendo às pessoas o que ela faz e, possivelmente, POR QUE
ela faz isso.

Ao comentar as funções da API do kernel, por favor use o formato kernel-doc.
Veja os arquivos em :ref:`Documentation/doc-guide/ <doc_guide>` e
``tools/docs/kernel-doc`` para detalhes. Observe que o perigo de comentar em
excesso se aplica aos comentários kernel-doc da mesma forma. Não adicione
kernel-doc genérico que apenas repete o que já é óbvio pela assinatura da
função.

O estilo preferido para comentários longos (em várias linhas) é:

.. code-block:: c

	/*
	 * Este é o estilo preferido para comentários em várias linhas
	 * no código-fonte do kernel Linux.
	 * Por favor, use-o de forma consistente.
	 *
	 * Descrição: uma coluna de asteriscos à esquerda,
	 * com linhas de início e fim quase vazias.
	 */

Também é importante comentar dados, sejam tipos básicos ou tipos derivados.
Para isso, use apenas uma declaração de dado por linha (sem vírgulas para
múltiplas declarações de dados). Isso deixa espaço para um pequeno comentário
em cada item explicando seu uso.


9) Você fez uma bagunça
-----------------------

Tudo bem, todos fazemos isso. Você provavelmente foi informado por seu
auxiliar de longa data em Unix que o ``GNU emacs`` formata automaticamente os
arquivos-fonte em C para você, e você percebeu que ele realmente faz isso, mas as
configurações padrão que ele usa são menos do que desejáveis (na verdade,
elas são piores do que digitação aleatória - um número infinito de macacos
digitando no GNU emacs nunca faria um bom programa).

Então, você pode ou se livrar do GNU emacs, ou mudar para usar valores mais
sãos. Para fazer isso, você pode colocar o seguinte no seu arquivo .emacs:

.. code-block:: elisp

  (defun c-lineup-arglist-tabs-only (ignored)
    "Line up argument lists by tabs, not spaces"
    (let* ((anchor (c-langelem-pos c-syntactic-element))
          (column (c-langelem-2nd-pos c-syntactic-element))
          (offset (- (1+ column) anchor))
          (steps (floor offset c-basic-offset)))
      (* (max steps 1)
         c-basic-offset)))

  (dir-locals-set-class-variables
   'linux-kernel
   '((c-mode . (
          (c-basic-offset . 8)
          (c-label-minimum-indentation . 0)
          (c-offsets-alist . (
                  (arglist-close         . c-lineup-arglist-tabs-only)
                  (arglist-cont-nonempty .
                      (c-lineup-gcc-asm-reg c-lineup-arglist-tabs-only))
                  (arglist-intro         . +)
                  (brace-list-intro      . +)
                  (c                     . c-lineup-C-comments)
                  (case-label            . 0)
                  (comment-intro         . c-lineup-comment)
                  (cpp-define-intro      . +)
                  (cpp-macro             . -1000)
                  (cpp-macro-cont        . +)
                  (defun-block-intro     . +)
                  (else-clause           . 0)
                  (func-decl-cont        . +)
                  (inclass               . +)
                  (inher-cont            . c-lineup-multi-inher)
                  (knr-argdecl-intro     . 0)
                  (label                 . -1000)
                  (statement             . 0)
                  (statement-block-intro . +)
                  (statement-case-intro  . +)
                  (statement-cont        . +)
                  (substatement          . +)
                  ))
          (indent-tabs-mode . t)
          (show-trailing-whitespace . t)
          ))))

  (dir-locals-set-directory-class
   (expand-file-name "~/src/linux-trees")
   'linux-kernel)

Isso fará o emacs funcionar melhor com o estilo de codificação do kernel para
arquivos C abaixo de ``~/src/linux-trees``.

Mas, mesmo que você falhe em fazer o emacs formatar de maneira sensata, nem
tudo está perdido: use ``indent``.

Agora, novamente, o GNU indent tem as mesmas configurações sem cérebro do GNU
emacs, e é por isso que você precisa dar a ele algumas opções de linha de
comando. No entanto, isso não é tão ruim, porque até os criadores do GNU
indent reconhecem a autoridade do K&R (as pessoas do GNU não são más, apenas
estão gravemente equivocadas nessa questão), então você apenas dá ao indent as
opções ``-kr -i8`` (que significa ``K&R, indentações de 8 caracteres``), ou
usa ``scripts/Lindent``, que indenta no estilo mais recente.

``indent`` tem muitas opções, e especialmente quando se trata de reformatação
de comentários, você pode querer dar uma olhada na página de manual. Mas
lembre-se: ``indent`` não é uma solução para programação ruim.

Observe que você também pode usar a ferramenta ``clang-format`` para ajudá-lo
com essas regras, para reformatar rapidamente partes do seu código
automaticamente e revisar arquivos completos para detectar erros de estilo de
codificação, erros de digitação e possíveis melhorias. Também é útil para
ordenar ``#includes``, alinhar variáveis/macros, reorganizar texto e outras
tarefas semelhantes. Consulte o arquivo
:ref:`Documentation/dev-tools/clang-format.rst <clangformat>`
para obter mais detalhes.

Algumas configurações básicas do editor, como indentação e finais de linha,
serão definidas automaticamente se você estiver usando um editor compatível com
o EditorConfig. Consulte o site oficial do EditorConfig para obter mais
informação: https://editorconfig.org/

10) Arquivos de configuração Kconfig
------------------------------------

Para todos os arquivos de configuração Kconfig* em toda a árvore de origem,
a indentação é um pouco diferente. Linhas sob uma definição ``config`` são
indentadas com uma tabulação, enquanto o texto de ajuda é indentado com mais
dois espaços. Exemplo::

  config AUDIT
	bool "Auditing support"
	depends on NET
	help
	  Enable auditing infrastructure that can be used with another
	  kernel subsystem, such as SELinux (which requires this for
	  logging of avc messages output).  Does not do system-call
	  auditing without CONFIG_AUDITSYSCALL.

Recursos seriamente perigosos (como suporte de gravação para certos sistemas
de arquivos) devem anunciar isso de forma proeminente na string do prompt::

  config ADFS_FS_RW
	bool "ADFS write support (DANGEROUS)"
	depends on ADFS_FS
	...

Para documentação completa sobre os arquivos de configuração, consulte o
arquivo Documentation/kbuild/kconfig-language.rst.


11) Estruturas de dados
-----------------------

Estruturas de dados que tenham visibilidade fora do ambiente monothread em que
são criadas e destruídas devem ter contadores de referência. No kernel, coleta
de lixo não existe (e fora do kernel, a coleta de lixo é lenta e
ineficiente), o que significa que você absolutamente **precisa** contar todas
as referências de uso.

Contagem de referência significa que você pode evitar bloqueios e permite que
múltiplos usuários tenham acesso à estrutura de dados em paralelo - sem se
preocupar com a estrutura desaparecendo debaixo deles do nada apenas porque
eles dormiram ou fizeram outra coisa por um tempo.

Observe que bloqueio **não** substitui contagem de referência. O bloqueio é
usado para manter estruturas de dados coerentes, enquanto a contagem de
referência é uma técnica de gerenciamento de memória. Normalmente, ambos são
necessários, e não devem ser confundidos entre si.

Muitas estruturas de dados podem, de fato, ter dois níveis de contagem de
referência, quando existem usuários de diferentes ``classes``. A contagem da
subclasse conta o número de usuários da subclasse e decrementa a contagem
global apenas uma vez quando a contagem da subclasse chega a zero.

Exemplos desse tipo de ``multi-level-reference-counting`` podem ser encontrados
em gerenciamento de memória (``struct mm_struct``: mm_users e mm_count) e em
código de sistema de arquivos (``struct super_block``: s_count e s_active).

Lembre-se: se outra thread puder encontrar sua estrutura de dados, e você não
tiver contagem de referência nela, quase certamente há um bug.


12) Macros, enums e RTL
-----------------------

Nomes de macros que definem constantes e rótulos em enums são maiúsculos.

.. code-block:: c

	#define CONSTANT 0x12345

Enums são preferidos quando várias constantes relacionadas são definidas.

Nomes de macro em maiúsculas são apreciados, mas macros que se parecem com
funções podem ser nomeadas em minúsculas.

Em geral, funções inline são preferíveis a macros que se parecem com funções.

Macros com múltiplas instruções devem ser envoltas em um bloco do-while:

.. code-block:: c

	#define macrofun(a, b, c) \
		do { \
			if (a == 5) \
				do_this(b, c); \
		} while (0)

Macros do tipo função com parâmetros não usados devem ser substituídas por
funções estáticas inline para evitar o problema de variáveis não usadas:

.. code-block:: c

	static inline void fun(struct foo *foo)
	{
	}

Devido a práticas históricas, muitos arquivos ainda empregam a abordagem
"cast para (void)" para avaliar parâmetros. No entanto, esse método não é
aconselhável.
Funções inline resolvem o problema de "expressão com efeitos colaterais
avaliada mais de uma vez", contornam problemas de variáveis não usadas e, por
algum motivo, geralmente são mais bem documentadas do que macros.

.. code-block:: c

	/*
	 * Evite fazer isto sempre que possível e prefira funções estáticas
	 * inline
	 */
	#define macrofun(foo) do { (void) (foo); } while (0)

Coisas a evitar ao usar macros:

1) macros que afetam o fluxo de controle:

.. code-block:: c

	#define FOO(x) \
		do { \
			if (blah(x) < 0) \
				return -EBUGGERED; \
		} while (0)

é uma ideia **muito** ruim. Ela parece uma chamada de função, mas sai da
função ``calling``; não quebre os parsers internos de quem lerá o código.

2) macros que dependem de ter uma variável local com um nome mágico:

.. code-block:: c

	#define FOO(val) bar(index, val)

pode parecer uma boa coisa, mas é confuso pra caramba para quem lê o código e
é propenso a quebrar com mudanças aparentemente inocentes.

3) macros com argumentos usados como l-values: FOO(x) = y; vai te morder se
alguém, por exemplo, transformar FOO em uma função inline.

4) esquecer da precedência: macros que definem constantes usando expressões
devem colocar a expressão entre parênteses. Cuidado com problemas semelhantes
com macros que usam parâmetros.

.. code-block:: c

	#define CONSTANT 0x4000
	#define CONSTEXP (CONSTANT | 3)

5) colisões de namespace ao definir variáveis locais em macros que se
parecem com funções:

.. code-block:: c

	#define FOO(x)				\
	({					\
		typeof(x) ret;			\
		ret = calc_ret(x);		\
		(ret);				\
	})

``ret`` é um nome comum para uma variável local - ``__foo_ret`` tem menos
chance de colidir com uma variável existente.

O manual do cpp trata de macros de forma exaustiva. O manual interno do gcc
também cobre o RTL, que é usado frequentemente com linguagem de montagem no
kernel.


13) Imprimindo mensagens do kernel
----------------------------------

Desenvolvedores do kernel gostam de ser vistos como letrados. Preste atenção à
ortografia das mensagens do kernel para causar uma boa impressão. Não use
contrações incorretas como ``dont``; use ``do not`` ou ``don't`` em vez
disso. Faça as mensagens concisas, claras e inequívocas.

Mensagens do kernel não precisam terminar com ponto.

Imprimir números entre parênteses (%d) não agrega valor e deve ser evitado.

Há vários macros de diagnóstico do modelo de driver em <linux/dev_printk.h>
que você deve usar para garantir que as mensagens sejam correspondidas ao
dispositivo e driver corretos e sejam marcadas com o nível certo: ``dev_err()``,
``dev_warn()``, ``dev_info()`` e assim por diante. Para mensagens que não
estão associadas a um device específico, <linux/printk.h> define
``pr_notice()``, ``pr_info()``, ``pr_warn()``, ``pr_err()`` etc. Quando os
drivers funcionam corretamente, eles ficam silenciosos, então prefira usar
``dev_dbg``/``pr_debug`` a menos que algo esteja errado.

Encontrar boas mensagens de depuração pode ser um desafio; e, uma vez que
você tenha essas mensagens, elas podem ajudar bastante para solução de
problemas remota. No entanto, a impressão de mensagens de depuração é tratada
diferentemente da impressão de outras mensagens não de depuração. Enquanto as
outras funções ``pr_XXX()`` imprimem incondicionalmente, ``pr_debug()`` não;
ela é compilada fora por padrão, a menos que ``DEBUG`` seja definido ou
``CONFIG_DYNAMIC_DEBUG`` esteja configurado. Isso também vale para
``dev_dbg()``, e uma convenção relacionada usa ``VERBOSE_DEBUG`` para adicionar
mensagens ``dev_vdbg()`` às já habilitadas por ``DEBUG``.

Muitos subsistemas têm opções de depuração do Kconfig para ativar ``-DDEBUG``
no Makefile correspondente; em outros casos, arquivos específicos fazem
``#define DEBUG``. E quando uma mensagem de depuração deve ser impressa
incondicionalmente, por exemplo, se ela já estiver dentro de uma seção
``#ifdef`` relacionada à depuração, pode-se usar ``printk(KERN_DEBUG ...)``.


14) Alocando memória
--------------------

O kernel fornece os seguintes alocadores de memória de uso geral:
``kmalloc()``, ``kzalloc()``, ``kmalloc_objs()``, ``kzalloc_objs()``,
``vmalloc()`` e ``vzalloc()``. Consulte a documentação da API para obter mais
informações sobre eles. :ref:`Documentation/core-api/memory-allocation.rst
<memory_allocation>`

A forma preferida de passar o tamanho de uma estrutura é a seguinte:

.. code-block:: c

	p = kmalloc_obj(*p, ...);

A forma alternativa em que o nome da estrutura é escrito explicitamente piora a
legibilidade e cria oportunidade para um bug quando o tipo da variável ponteiro
é alterado, mas o ``sizeof`` correspondente passado para um alocador de memória
não é.

Casting do valor de retorno, que é um ponteiro ``void``, é redundante. A
conversão de ponteiro ``void`` para qualquer outro tipo de ponteiro é garantida
pela linguagem de programação C.

A forma preferida para alocar um array é a seguinte:

.. code-block:: c

	p = kmalloc_objs(*p, n, ...);

A forma preferida para alocar um array zerado é a seguinte:

.. code-block:: c

	p = kzalloc_objs(*p, n, ...);

As duas formas verificam estouro no tamanho de alocação ``n * sizeof(...)`` e
retornam ``NULL`` se isso ocorrer.

Essas funções genéricas de alocação emitem um dump de pilha em caso de falha
quando usadas sem ``__GFP_NOWARN``, então não há utilidade em emitir uma
mensagem de falha adicional quando ``NULL`` é retornado.

15) A doença do inline
----------------------

Parece haver uma percepção errônea comum de que o gcc tem uma opção mágica de
aceleração chamada ``inline``. Embora o uso de ``inline`` possa ser apropriado
(por exemplo, como uma forma de substituir macros; veja o Capítulo 12), muitas
vezes não é. O uso abundante da palavra-chave ``inline`` leva a um kernel
muito maior, o que, por sua vez, torna o sistema mais lento como um todo, por
causa de uma maior ocupação de i-cache para a CPU e simplesmente porque há menos
memória disponível para o ``pagecache``. Pense nisso: uma falha no pagecache
causa um seek no disco, que facilmente leva 5 milissegundos. Há MUITOS ciclos
de CPU que podem entrar nesses 5 milissegundos.

Uma regra prática razoável é não colocar ``inline`` em funções com mais de 3
linhas de código. Uma exceção a essa regra são os casos em que um parâmetro é
conhecido como uma constante em tempo de compilação, e como resultado dessa
constância você *sabe* que o compilador será capaz de otimizar grande parte da
sua função em tempo de compilação. Para um bom exemplo desse caso posterior,
veja a função inline ``kmalloc()``.

Muitas pessoas argumentam que adicionar ``inline`` a funções ``static`` usadas
apenas uma vez é sempre uma vantagem, porque não há custo de espaço. Embora
isso seja tecnicamente correto, o gcc é capaz de fazer esse inline
automaticamente sem ajuda, e a questão de manutenção de remover o ``inline``
quando um segundo usuário aparece supera o valor potencial da dica que diz ao
gcc para fazer algo que ele faria de qualquer forma.


16) Valores e nomes de retorno de função
----------------------------------------

Funções podem retornar valores de vários tipos, e um dos mais comuns é um valor
que indica se a função teve sucesso ou falhou. Esse valor pode ser representado
como um inteiro de código de erro (-Exxx = falha, 0 = sucesso) ou como um
booleano ``succeeded`` (0 = falha, diferente de zero = sucesso).

Misturar esses dois tipos de representação é uma fonte fértil de bugs difíceis
de encontrar. Se a linguagem C incluísse uma distinção forte entre inteiros e
booleanos, o compilador encontraria esses erros para nós... mas não inclui. Para
ajudar a evitar esses bugs, siga sempre esta convenção::

	Se o nome de uma função for uma ação ou um comando imperativo,
	a função deve retornar um inteiro de código de erro. Se o nome
	for um predicado, a função deve retornar um booleano de "sucesso".

Por exemplo, ``add work`` é um comando, e a função ``add_work()`` retorna 0
para sucesso ou -EBUSY para falha. Da mesma forma, ``PCI device present`` é um
predicado, e a função ``pci_dev_present()`` retorna 1 se encontrar um device
correspondente ou 0 se não encontrar.

Todas as funções ``EXPORT`` devem respeitar esta convenção, e assim também
devem todas as funções públicas. Funções privadas (``static``) não precisam,
mas é recomendável que o façam.

Funções cujo valor de retorno é o resultado real de um cálculo, em vez de uma
indicação de se o cálculo teve sucesso, não estão sujeitas a essa regra.
Normalmente, elas indicam falha retornando algum resultado fora do intervalo.
Exemplos típicos seriam funções que retornam ponteiros; elas usam ``NULL`` ou o
mecanismo ``ERR_PTR`` para informar falha.


17) Usando bool
---------------

O tipo ``bool`` do kernel Linux é um alias do tipo C99 ``_Bool``. Valores
``bool`` só podem avaliar para 0 ou 1, e conversão implícita ou explícita para
``bool`` converte automaticamente o valor para verdadeiro ou falso. Ao usar
tipos ``bool``, a construção ``!!`` não é necessária, o que elimina uma classe
de bugs.

Ao trabalhar com valores ``bool``, as definições ``true`` e ``false`` devem ser
usadas em vez de 1 e 0.

Tipos de retorno de função ``bool`` e variáveis locais na pilha são sempre
válidos quando apropriados. O uso de ``bool`` é encorajado para melhorar a
legibilidade e muitas vezes é uma opção melhor do que ``int`` para armazenar
valores booleanos.

Não use ``bool`` se o layout da linha de cache ou o tamanho do valor importar,
porque seu tamanho e alinhamento variam conforme a arquitetura compilada.
Estruturas otimizadas para alinhamento e tamanho não devem usar ``bool``.

Se uma estrutura tiver muitos valores verdadeiro/falso, considere consolidá-los
em um ``bitfield`` com membros de 1 bit, ou usar um tipo de largura fixa
apropriado, como ``u8``.

Da mesma forma, para argumentos de função, muitos valores verdadeiro/falso podem
ser consolidados em um único argumento de sinalizadores bit a bit, e
``flags`` muitas vezes pode ser uma alternativa mais legível se os pontos de
chamada tiverem constantes verdadeiras/falsas "nuas".

Caso contrário, o uso limitado de ``bool`` em estruturas e argumentos pode
melhorar a legibilidade.

18) Não reinventando as macros do kernel
----------------------------------------

Existem muitos arquivos de cabeçalho em ``include/linux/`` que contêm várias
macros que você deve usar em vez de escrever explicitamente alguma variante
delas. Por exemplo, se você precisa calcular o comprimento de um array, aproveite
a macro

.. code-block:: c

	#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

que é definida em ``array_size.h``.

Da mesma forma, se você precisar calcular o tamanho de um membro de alguma
estrutura, use

.. code-block:: c

	#define sizeof_field(t, f) (sizeof(((t*)0)->f))

que é definida em ``stddef.h``.

Também existem macros ``min()`` e ``max()`` definidas em ``minmax.h`` que fazem
verificação estrita de tipos se você precisar delas. Sinta-se à vontade para
explorar os arquivos de cabeçalho para ver o que já está definido e não deve
ser reproduzido no seu código.


19) Modelines do editor e outros restos
---------------------------------------

Alguns editores podem interpretar informações de configuração embutidas em
arquivos de origem, indicadas por marcadores especiais. Por exemplo, o emacs
interpreta linhas marcadas assim:

.. code-block:: c

	-*- mode: c -*-

Ou assim:

.. code-block:: c

	/*
	Local Variables:
	compile-command: "gcc -DMAGIC_DEBUG_FLAG foo.c"
	End:
	*/

O Vim interpreta marcadores que parecem com isto:

.. code-block:: c

	/* vim:set sw=8 noet */

Não inclua nenhum desses em arquivos de origem. As pessoas têm suas próprias
configurações pessoais de editor, e seus arquivos de origem não devem
substituí-las. Isso inclui marcadores para indentação e configuração de modo.
As pessoas podem usar seu próprio modo personalizado, ou podem ter algum outro
método mágico para fazer a indentação funcionar corretamente.


20) Montagem inline
-------------------

Em código específico de arquitetura, pode ser necessário usar montagem inline
para interagir com a funcionalidade da CPU ou da plataforma. Não hesite em
fazê-lo quando necessário. No entanto, não use montagem inline de forma
gratuita quando o C puder fazer o trabalho. Você pode e deve mexer em hardware
em C quando possível.

Considere escrever funções auxiliares simples que encapsulem partes comuns de
montagem inline, em vez de escrevê-las repetidamente com pequenas variações.
Lembre-se de que a montagem inline pode usar parâmetros C.

Funções grandes e não triviais de montagem devem ir para arquivos ``.S``, com
protótipos C correspondentes definidos em arquivos de cabeçalho C. Os
protótipos C para funções de montagem devem usar ``asmlinkage``.

Você pode precisar marcar sua instrução ``asm`` como ``volatile`` para impedir
que o GCC a remova se o GCC não perceber efeitos colaterais. No entanto, você
nem sempre precisa fazer isso, e fazê-lo desnecessariamente pode limitar a
otimização.

Ao escrever uma única instrução de montagem inline contendo várias instruções,
coloque cada instrução em uma linha separada em uma string separada e termine
cada string, exceto a última, com ``\n\t`` para indentar corretamente a
próxima instrução na saída de montagem:

.. code-block:: c

	asm ("magic %reg1, #42\n\t"
	     "more_magic %reg2, %reg3"
	     : /* outputs */ : /* inputs */ : /* clobbers */);


21) Compilação condicional
--------------------------

Sempre que possível, não use condicionais do pré-processador (#if, #ifdef) em
arquivos ``.c``; isso torna o código mais difícil de ler e a lógica mais difícil
de seguir. Em vez disso, use esses condicionais em um arquivo de cabeçalho que
defina funções para uso nesses arquivos ``.c``, fornecendo versões de stub sem
efeito no caso ``#else``, e então chame essas funções incondicionalmente nos
arquivos ``.c``. O compilador evitará gerar qualquer código para as chamadas de
stub, produzindo resultados idênticos, mas a lógica permanecerá fácil de
seguir.

Prefira compilar funções inteiras fora do código, em vez de partes de funções
ou partes de expressões. Em vez de colocar um ``ifdef`` em uma expressão,
extraia parte ou toda a expressão para uma função auxiliar separada e aplique a
condicional a essa função.

Se você tiver uma função ou variável que pode potencialmente ficar sem uso em
uma configuração específica, e o compilador avisaria sobre a definição ficar sem
uso, marque a definição como ``__maybe_unused`` em vez de envolvê-la em uma
condicional do pré-processador. (No entanto, se uma função ou variável
*sempre* ficar sem uso, elimine-a.)

Dentro do código, quando possível, use a macro ``IS_ENABLED`` para converter um
símbolo Kconfig em uma expressão booleana C e usá-la em uma condicional C
normal:

.. code-block:: c

	if (IS_ENABLED(CONFIG_SOMETHING)) {
		...
	}

O compilador reduzirá a condicional a um valor constante e incluirá ou
excluirá o bloco de código assim como com um ``#ifdef``, então isso não
adicionará nenhum custo de runtime. No entanto, essa abordagem ainda permite
que o compilador C veja o código dentro do bloco e verifique sua correção
(sintaxe, tipos, referências de símbolo etc.). Portanto, você ainda precisa usar
um ``#ifdef`` se o código dentro do bloco referenciar símbolos que não existirão
se a condição não for atendida.

No final de qualquer bloco ``#if`` ou ``#ifdef`` não trivial (mais de algumas
linhas), coloque um comentário após o ``#endif`` na mesma linha, indicando a
expressão condicional usada. Por exemplo:

.. code-block:: c

	#ifdef CONFIG_SOMETHING
	...
	#endif /* CONFIG_SOMETHING */


22) Não derrube o kernel
------------------------

Em geral, a decisão de derrubar o kernel pertence ao usuário, e não ao
desenvolvedor do kernel.

Evite ``panic()``
*****************

``panic()`` deve ser usado com cuidado e principalmente apenas durante a inicialização
do sistema. ``panic()`` é, por exemplo, aceitável ao ficar sem memória durante
a inicialização e não ser possível continuar.

Use ``WARN()`` em vez de ``BUG()``
**********************************

Não adicione novo código que use nenhuma das variantes de ``BUG()``, como
``BUG()``, ``BUG_ON()`` ou ``VM_BUG_ON()``. Em vez disso, use uma variante de
``WARN*()``, preferencialmente ``WARN_ON_ONCE()``, e possivelmente com código de
recuperação. O código de recuperação não é obrigatório se não houver uma
maneira razoável de pelo menos recuperar parcialmente.

"Sou preguiçoso para tratar erros" não é uma desculpa para usar ``BUG()``.
Corrupções internas graves sem como continuar ainda podem usar ``BUG()``, mas
precisam de uma boa justificativa.

Use ``WARN_ON_ONCE()`` em vez de ``WARN()`` ou ``WARN_ON()``
************************************************************

``WARN_ON_ONCE()`` geralmente é preferido em relação a ``WARN()`` ou
``WARN_ON()``, porque é comum que uma dada condição de aviso, se ocorrer,
ocorra várias vezes. Isso pode encher e sobrescrever o log do kernel e até
diminuir o sistema o suficiente para que o registro excessivo vire um problema
adicional.

Não emita ``WARN`` levianamente
*******************************

``WARN*()`` foi concebido para situações inesperadas, em que "isso nunca devia
acontecer". Macros ``WARN*()`` não devem ser usadas para nada que se espere que
aconteça durante a operação normal. Esses não são asserts de pré- ou pós-
condição, por exemplo. Novamente: ``WARN*()`` não deve ser usado para uma
condição que se espera que seja acionada facilmente, por exemplo, por ações do
espaço do usuário. ``pr_warn_once()`` é uma alternativa possível, se você
precisar notificar o usuário sobre um problema.

Não se preocupe com usuários de ``panic_on_warn``
*************************************************

Mais algumas palavras sobre ``panic_on_warn``: lembre-se de que
``panic_on_warn`` é uma opção disponível do kernel, e muitos usuários a
habilitam. É por isso que existe um texto "Não emita WARN levianamente" acima.
Entretanto, a existência de usuários de ``panic_on_warn`` não é uma razão válida
para evitar o uso judicioso de ``WARN*()``. Isso ocorre porque quem habilita
``panic_on_warn`` explicitamente pediu ao kernel para travar se um ``WARN*()``
for disparado, e esses usuários devem estar preparados para lidar com as
consequências de um sistema que tem uma chance um pouco maior de travar.

Use ``BUILD_BUG_ON()`` para assertivas em tempo de compilação
*************************************************************

O uso de ``BUILD_BUG_ON()`` é aceitável e encorajado, porque é uma assertiva
em tempo de compilação que não tem efeito em tempo de execução.

Apêndice I) Referências
-----------------------

The C Programming Language, Second Edition
by Brian W. Kernighan and Dennis M. Ritchie.
Prentice Hall, Inc., 1988.
ISBN 0-13-110362-8 (paperback), 0-13-110370-9 (hardback).

The Practice of Programming
by Brian W. Kernighan and Rob Pike.
Addison-Wesley, Inc., 1999.
ISBN 0-201-61586-X.

Manuais GNU - onde em conformidade com K&R e este texto - para cpp, gcc,
gcc internals e indent, todos disponíveis em https://www.gnu.org/manual/

WG14 é o grupo de trabalho de padronização internacional para a linguagem de
programação C, URL: http://www.open-std.org/JTC1/SC22/WG14/

Kernel CodingStyle, by greg@kroah.com at OLS 2002:
http://www.kroah.com/linux/talks/ols_2002_kernel_codingstyle_talk/html/
